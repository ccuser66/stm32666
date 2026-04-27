package com.example.audiobluetoothmonitor

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothGattService
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothSocket
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.Handler
import android.os.Looper
import androidx.core.content.ContextCompat
import java.io.BufferedReader
import java.io.IOException
import java.io.InputStreamReader
import java.lang.reflect.InvocationTargetException
import java.nio.charset.StandardCharsets
import java.util.ArrayDeque
import java.util.LinkedHashMap
import java.util.UUID
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread

class BluetoothService(
    private val context: Context,
    private val onConnectionChanged: (String, Boolean) -> Unit,
    private val onLineReceived: (String) -> Unit,
) {
    private val adapter: BluetoothAdapter? = BluetoothAdapter.getDefaultAdapter()
    private var socket: BluetoothSocket? = null
    private var gatt: BluetoothGatt? = null
    private var bleWriteCharacteristic: BluetoothGattCharacteristic? = null
    private var bleNotifyCharacteristic: BluetoothGattCharacteristic? = null
    @Volatile private var bleReady = false
    @Volatile private var running = false
    @Volatile private var suppressBleDisconnectEvent = false
    private val bleBufferLock = Any()
    private val bleLineBuffer = StringBuilder()
    private val bleWriteLock = Any()
    private val bleWriteQueue = ArrayDeque<ByteArray>()
    @Volatile private var bleWriteInFlight = false
    private val mainHandler = Handler(Looper.getMainLooper())
    private var classicDiscoveryReceiver: BroadcastReceiver? = null
    private var bleScanner: BluetoothLeScanner? = null
    private var bleScanCallback: ScanCallback? = null
    private var bleScanTimeoutRunnable: Runnable? = null
    @Volatile private var discoveryActive = false
    @Volatile private var classicScanRunning = false
    @Volatile private var bleScanRunning = false
    private var onDeviceDiscovered: ((BluetoothDevice) -> Unit)? = null
    private var onScanStatusChanged: ((String) -> Unit)? = null
    private var onScanFinished: (() -> Unit)? = null

    fun isBluetoothSupported(): Boolean = adapter != null

    fun isBluetoothEnabled(): Boolean = adapter?.isEnabled == true

    fun isConnected(): Boolean = socket?.isConnected == true || (bleReady && gatt != null)

    @SuppressLint("MissingPermission")
    fun getPairedDevices(): List<BluetoothDevice> {
        val bonded = adapter?.bondedDevices ?: return emptyList()
        return bonded.sortedWith(compareBy({ it.name ?: "" }, { it.address }))
    }

    @SuppressLint("MissingPermission")
    fun startDeviceScan(
        onStatusChanged: (String) -> Unit,
        onDeviceFound: (BluetoothDevice) -> Unit,
        onFinished: () -> Unit,
    ) {
        val btAdapter = adapter ?: run {
            onStatusChanged("当前设备不支持蓝牙")
            onFinished()
            return
        }
        if (!btAdapter.isEnabled) {
            onStatusChanged("系统蓝牙未开启")
            onFinished()
            return
        }

        stopDeviceScan()
        discoveryActive = true
        onDeviceDiscovered = onDeviceFound
        onScanStatusChanged = onStatusChanged
        onScanFinished = onFinished

        getPairedDevices().forEach { device ->
            onDeviceFound(device)
        }

        var started = false
        if (startClassicDiscovery(btAdapter)) {
            started = true
        }
        if (startBleScan(btAdapter)) {
            started = true
        }

        if (!started) {
            finishDeviceScan("未能启动蓝牙扫描")
        } else {
            onStatusChanged("正在搜索附近蓝牙设备...")
        }
    }

    @SuppressLint("MissingPermission")
    fun stopDeviceScan() {
        discoveryActive = false
        onDeviceDiscovered = null
        onScanStatusChanged = null
        onScanFinished = null

        bleScanTimeoutRunnable?.let(mainHandler::removeCallbacks)
        bleScanTimeoutRunnable = null

        val scanner = bleScanner
        val callback = bleScanCallback
        bleScanner = null
        bleScanCallback = null
        bleScanRunning = false
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP && scanner != null && callback != null) {
            try {
                scanner.stopScan(callback)
            } catch (_: Exception) {
            }
        }

        val btAdapter = adapter
        if (btAdapter?.isDiscovering == true) {
            try {
                btAdapter.cancelDiscovery()
            } catch (_: Exception) {
            }
        }
        classicScanRunning = false
        unregisterClassicDiscoveryReceiver()
    }

    @SuppressLint("MissingPermission")
    private fun startClassicDiscovery(btAdapter: BluetoothAdapter): Boolean {
        registerClassicDiscoveryReceiver()
        return try {
            classicScanRunning = btAdapter.startDiscovery()
            classicScanRunning
        } catch (_: Exception) {
            classicScanRunning = false
            unregisterClassicDiscoveryReceiver()
            false
        }
    }

    @SuppressLint("MissingPermission")
    private fun startBleScan(btAdapter: BluetoothAdapter): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) {
            bleScanRunning = false
            return false
        }

        val scanner = btAdapter.bluetoothLeScanner ?: run {
            bleScanRunning = false
            return false
        }

        val callback = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                onDeviceDiscovered?.invoke(result.device)
            }

            override fun onBatchScanResults(results: MutableList<ScanResult>) {
                results.forEach { result ->
                    onDeviceDiscovered?.invoke(result.device)
                }
            }

            override fun onScanFailed(errorCode: Int) {
                bleScanRunning = false
                bleScanner = null
                bleScanCallback = null
                bleScanTimeoutRunnable?.let(mainHandler::removeCallbacks)
                bleScanTimeoutRunnable = null
                if (classicScanRunning) {
                    onScanStatusChanged?.invoke("BLE 扫描失败: $errorCode，继续搜索经典蓝牙设备")
                } else {
                    finishDeviceScan("BLE 扫描失败: $errorCode")
                }
            }
        }

        return try {
            bleScanner = scanner
            bleScanCallback = callback
            bleScanRunning = true
            scanner.startScan(callback)
            bleScanTimeoutRunnable = Runnable {
                stopBleScanAndCheckCompletion()
            }
            mainHandler.postDelayed(bleScanTimeoutRunnable!!, BLE_SCAN_TIMEOUT_MS)
            true
        } catch (_: Exception) {
            bleScanner = null
            bleScanCallback = null
            bleScanTimeoutRunnable = null
            bleScanRunning = false
            false
        }
    }

    private fun stopBleScanAndCheckCompletion() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            val scanner = bleScanner
            val callback = bleScanCallback
            bleScanner = null
            bleScanCallback = null
            if (scanner != null && callback != null) {
                try {
                    scanner.stopScan(callback)
                } catch (_: Exception) {
                }
            }
        }
        bleScanTimeoutRunnable?.let(mainHandler::removeCallbacks)
        bleScanTimeoutRunnable = null
        bleScanRunning = false
        checkScanCompletion()
    }

    private fun registerClassicDiscoveryReceiver() {
        if (classicDiscoveryReceiver != null) {
            return
        }

        classicDiscoveryReceiver = object : BroadcastReceiver() {
            override fun onReceive(context: Context?, intent: Intent?) {
                when (intent?.action) {
                    BluetoothAdapter.ACTION_DISCOVERY_STARTED -> {
                        onScanStatusChanged?.invoke("正在搜索经典蓝牙设备，并补充设备名称...")
                    }

                    BluetoothDevice.ACTION_FOUND -> {
                        extractDevice(intent)?.let { device ->
                            onDeviceDiscovered?.invoke(device)
                        }
                    }

                    BluetoothDevice.ACTION_NAME_CHANGED -> {
                        extractDevice(intent)?.let { device ->
                            onDeviceDiscovered?.invoke(device)
                        }
                    }

                    BluetoothAdapter.ACTION_DISCOVERY_FINISHED -> {
                        classicScanRunning = false
                        unregisterClassicDiscoveryReceiver()
                        checkScanCompletion()
                    }
                }
            }
        }

        val filter = IntentFilter().apply {
            addAction(BluetoothAdapter.ACTION_DISCOVERY_STARTED)
            addAction(BluetoothAdapter.ACTION_DISCOVERY_FINISHED)
            addAction(BluetoothDevice.ACTION_FOUND)
            addAction(BluetoothDevice.ACTION_NAME_CHANGED)
        }
        ContextCompat.registerReceiver(
            context,
            classicDiscoveryReceiver,
            filter,
            ContextCompat.RECEIVER_NOT_EXPORTED,
        )
    }

    private fun unregisterClassicDiscoveryReceiver() {
        val receiver = classicDiscoveryReceiver ?: return
        try {
            context.unregisterReceiver(receiver)
        } catch (_: Exception) {
        }
        classicDiscoveryReceiver = null
    }

    private fun checkScanCompletion() {
        if (!discoveryActive || classicScanRunning || bleScanRunning) {
            return
        }
        finishDeviceScan("附近蓝牙设备搜索完成")
    }

    private fun finishDeviceScan(message: String) {
        discoveryActive = false
        unregisterClassicDiscoveryReceiver()
        onScanStatusChanged?.invoke(message)
        onScanFinished?.invoke()
        onDeviceDiscovered = null
        onScanStatusChanged = null
        onScanFinished = null
    }

    private fun extractDevice(intent: Intent): BluetoothDevice? {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
        }
    }

    @SuppressLint("MissingPermission")
    fun connect(device: BluetoothDevice) {
        thread(name = "bluetooth-connect") {
            disconnectInternal(notify = false)
            try {
                val btAdapter = adapter ?: run {
                    onConnectionChanged("当前设备不支持蓝牙", false)
                    return@thread
                }
                if (!btAdapter.isEnabled) {
                    onConnectionChanged("系统蓝牙未开启", false)
                    return@thread
                }
                stopDeviceScan()
                btAdapter.cancelDiscovery()

                when (preferredTransport(device)) {
                    Transport.BLE -> connectBle(device)
                    Transport.CLASSIC -> connectClassic(device)
                }
            } catch (ex: IOException) {
                disconnectInternal(notify = false)
                onConnectionChanged(
                    "连接失败: ${device.name ?: device.address}，${ex.localizedMessage ?: "请确认设备已配对并允许连接"}",
                    false,
                )
            }
        }
    }

    private fun preferredTransport(device: BluetoothDevice): Transport {
        return if (device.type == BluetoothDevice.DEVICE_TYPE_LE) {
            Transport.BLE
        } else {
            Transport.CLASSIC
        }
    }

    @SuppressLint("MissingPermission")
    private fun connectClassic(device: BluetoothDevice) {
        val tmpSocket = connectSocket(device)
        socket = tmpSocket
        running = true
        onConnectionChanged("已连接 ${device.name ?: device.address}", true)
        startClassicReadLoop(tmpSocket)
    }

    @SuppressLint("MissingPermission")
    private fun connectBle(device: BluetoothDevice) {
        val state = BleConnectState()
        val callback = object : BluetoothGattCallback() {
            override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
                when {
                    status != BluetoothGatt.GATT_SUCCESS -> {
                        state.fail(IOException("BLE 连接状态错误: $status"), gatt)
                    }
                    newState == BluetoothProfile.STATE_CONNECTED -> {
                        if (!gatt.discoverServices()) {
                            state.fail(IOException("BLE 服务发现启动失败"), gatt)
                        }
                    }
                    newState == BluetoothProfile.STATE_DISCONNECTED -> {
                        if (state.completed) {
                            handleBleDisconnected()
                        } else {
                            state.fail(IOException("BLE 设备主动断开"), gatt)
                        }
                    }
                }
            }

            override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    state.fail(IOException("BLE 服务发现失败: $status"), gatt)
                    return
                }

                val selection = selectBleUartCharacteristics(gatt.services)
                if (selection == null) {
                    state.fail(IOException("未找到可用 BLE 串口特征: ${bleServicesSummary(gatt.services)}"), gatt)
                    return
                }

                bleWriteCharacteristic = selection.writeCharacteristic
                bleNotifyCharacteristic = selection.notifyCharacteristic

                val notifyCharacteristic = selection.notifyCharacteristic
                if (notifyCharacteristic == null) {
                    markBleReady(device, state)
                    return
                }

                if (!enableNotifications(gatt, notifyCharacteristic)) {
                    state.fail(IOException("BLE 通知开启失败"), gatt)
                    return
                }

                val cccd = notifyCharacteristic.getDescriptor(CLIENT_CONFIG_UUID)
                if (cccd == null) {
                    markBleReady(device, state)
                }
            }

            override fun onDescriptorWrite(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
                if (descriptor.uuid == CLIENT_CONFIG_UUID) {
                    if (status == BluetoothGatt.GATT_SUCCESS) {
                        markBleReady(device, state)
                    } else {
                        state.fail(IOException("BLE 通知描述符写入失败: $status"), gatt)
                    }
                }
            }

            override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
                handleBleBytes(characteristic.value ?: ByteArray(0))
            }

            override fun onCharacteristicChanged(
                gatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic,
                value: ByteArray,
            ) {
                handleBleBytes(value)
            }

            override fun onCharacteristicWrite(
                gatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic,
                status: Int,
            ) {
                if (characteristic.uuid != bleWriteCharacteristic?.uuid) {
                    return
                }

                if (status == BluetoothGatt.GATT_SUCCESS) {
                    onBleWriteFinished()
                } else {
                    handleBleWriteFailure(IOException("BLE 写入失败: $status"))
                }
            }
        }

        val activeGatt = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            device.connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE)
        } else {
            device.connectGatt(context, false, callback)
        } ?: throw IOException("BLE GATT 初始化失败")

        gatt = activeGatt

        if (!state.latch.await(BLE_CONNECT_TIMEOUT_MS, TimeUnit.MILLISECONDS)) {
            throw IOException("BLE GATT 连接超时")
        }

        state.error?.let { throw it }
    }

    @SuppressLint("MissingPermission")
    private fun markBleReady(device: BluetoothDevice, state: BleConnectState) {
        if (state.completed) {
            return
        }
        state.succeed()
        bleReady = true
        running = true
        onConnectionChanged("已连接 ${device.name ?: device.address}", true)
    }

    private fun handleBleDisconnected() {
        if (suppressBleDisconnectEvent) {
            suppressBleDisconnectEvent = false
            return
        }
        disconnectInternal(notify = false)
        onConnectionChanged("连接已断开", false)
    }

    private fun selectBleUartCharacteristics(services: List<BluetoothGattService>): BleUartSelection? {
        services.firstOrNull { it.uuid == NORDIC_UART_SERVICE_UUID }?.let { service ->
            val writeCharacteristic = service.getCharacteristic(NORDIC_UART_WRITE_UUID)
            val notifyCharacteristic = service.getCharacteristic(NORDIC_UART_NOTIFY_UUID)
            if (writeCharacteristic != null && notifyCharacteristic != null) {
                return BleUartSelection(writeCharacteristic, notifyCharacteristic)
            }
        }

        services.forEach { service ->
            service.characteristics.firstOrNull { characteristic ->
                canWrite(characteristic) && canNotify(characteristic)
            }?.let { characteristic ->
                return BleUartSelection(characteristic, characteristic)
            }

            val writeCharacteristic = service.characteristics
                .filter(::canWrite)
                .maxByOrNull(::writeScore)
            val notifyCharacteristic = service.characteristics
                .filter(::canNotify)
                .maxByOrNull(::notifyScore)
            if (writeCharacteristic != null && notifyCharacteristic != null) {
                return BleUartSelection(writeCharacteristic, notifyCharacteristic)
            }
        }

        val allCharacteristics = services.flatMap { it.characteristics }
        val writeCharacteristic = allCharacteristics
            .filter(::canWrite)
            .maxByOrNull(::writeScore)
            ?: return null
        val notifyCharacteristic = allCharacteristics
            .filter(::canNotify)
            .maxByOrNull(::notifyScore)

        return BleUartSelection(writeCharacteristic, notifyCharacteristic)
    }

    private fun writeScore(characteristic: BluetoothGattCharacteristic): Int {
        val uuidText = characteristic.uuid.toString().uppercase()
        var score = 0
        if (characteristic.properties and BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE != 0) score += 4
        if (characteristic.properties and BluetoothGattCharacteristic.PROPERTY_WRITE != 0) score += 3
        if (characteristic.properties and BluetoothGattCharacteristic.PROPERTY_NOTIFY != 0) score += 2
        if (uuidText.contains("FFE1") || uuidText.contains("FFF1") || uuidText.contains("FFF2") || uuidText.contains("6E400002")) score += 5
        return score
    }

    private fun notifyScore(characteristic: BluetoothGattCharacteristic): Int {
        val uuidText = characteristic.uuid.toString().uppercase()
        var score = 0
        if (characteristic.properties and BluetoothGattCharacteristic.PROPERTY_NOTIFY != 0) score += 5
        if (characteristic.properties and BluetoothGattCharacteristic.PROPERTY_INDICATE != 0) score += 4
        if (characteristic.properties and BluetoothGattCharacteristic.PROPERTY_READ != 0) score += 1
        if (uuidText.contains("FFE1") || uuidText.contains("FFF1") || uuidText.contains("FFF4") || uuidText.contains("6E400003")) score += 5
        return score
    }

    private fun canWrite(characteristic: BluetoothGattCharacteristic): Boolean {
        return characteristic.properties and BluetoothGattCharacteristic.PROPERTY_WRITE != 0 ||
            characteristic.properties and BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE != 0
    }

    private fun canNotify(characteristic: BluetoothGattCharacteristic): Boolean {
        return characteristic.properties and BluetoothGattCharacteristic.PROPERTY_NOTIFY != 0 ||
            characteristic.properties and BluetoothGattCharacteristic.PROPERTY_INDICATE != 0
    }

    private fun enableNotifications(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic): Boolean {
        if (!gatt.setCharacteristicNotification(characteristic, true)) {
            return false
        }

        val descriptor = characteristic.getDescriptor(CLIENT_CONFIG_UUID) ?: return true
        descriptor.value = if (characteristic.properties and BluetoothGattCharacteristic.PROPERTY_INDICATE != 0) {
            BluetoothGattDescriptor.ENABLE_INDICATION_VALUE
        } else {
            BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
        }
        return gatt.writeDescriptor(descriptor)
    }

    private fun handleBleBytes(bytes: ByteArray) {
        if (bytes.isEmpty()) {
            return
        }

        val text = bytes.toString(StandardCharsets.UTF_8)
        val lines = mutableListOf<String>()
        synchronized(bleBufferLock) {
            for (ch in text) {
                when (ch) {
                    '\r' -> Unit
                    '\n' -> {
                        lines += bleLineBuffer.toString()
                        bleLineBuffer.setLength(0)
                    }
                    else -> bleLineBuffer.append(ch)
                }
            }
        }
        lines.filter { it.isNotEmpty() }.forEach(onLineReceived)
    }

    @SuppressLint("MissingPermission")
    private fun connectSocket(device: BluetoothDevice): BluetoothSocket {
        val preferInsecure = prefersInsecureSocket(device)
        val socketFactories = LinkedHashMap<String, SocketFactoryOption>()

        fun add(option: SocketFactoryOption) {
            socketFactories.putIfAbsent(option.label, option)
        }

        if (preferInsecure) {
            add(SocketFactoryOption("insecure-channel-1") { target ->
                createChannelOneSocket(target, insecure = true)
            })
            add(SocketFactoryOption("insecure-spp") { target ->
                target.createInsecureRfcommSocketToServiceRecord(SPP_UUID)
            })
        } else {
            add(SocketFactoryOption("secure-spp") { target ->
                target.createRfcommSocketToServiceRecord(SPP_UUID)
            })
            add(SocketFactoryOption("insecure-spp") { target ->
                target.createInsecureRfcommSocketToServiceRecord(SPP_UUID)
            })
        }

        advertisedSocketFactories(device, preferInsecure).forEach(::add)

        add(SocketFactoryOption("secure-channel-1") { target ->
            createChannelOneSocket(target, insecure = false)
        })
        add(SocketFactoryOption("secure-spp") { target ->
            target.createRfcommSocketToServiceRecord(SPP_UUID)
        })
        add(SocketFactoryOption("insecure-channel-1") { target ->
            createChannelOneSocket(target, insecure = true)
        })
        add(SocketFactoryOption("insecure-spp") { target ->
            target.createInsecureRfcommSocketToServiceRecord(SPP_UUID)
        })

        val attemptErrors = mutableListOf<String>()
        var lastError: IOException? = null
        for (factory in socketFactories.values) {
            var candidate: BluetoothSocket? = null
            try {
                adapter?.cancelDiscovery()
                candidate = factory.create(device)
                candidate.connect()
                return candidate
            } catch (ex: IOException) {
                val reason = "${factory.label}: ${ex.localizedMessage ?: "连接失败"}"
                attemptErrors += reason
                lastError = IOException(reason, ex)
            } catch (ex: RuntimeException) {
                val reason = "${factory.label}: ${ex.localizedMessage ?: "连接失败"}"
                attemptErrors += reason
                lastError = IOException(reason, ex)
            } finally {
                try {
                    if (candidate != null && !candidate.isConnected) {
                        candidate.close()
                    }
                } catch (_: IOException) {
                }
            }
        }

        if (attemptErrors.isNotEmpty()) {
            throw IOException("${deviceSummary(device)} | ${attemptErrors.joinToString(" | ")}")
        }

        throw lastError ?: IOException("无法建立蓝牙连接")
    }

    @SuppressLint("MissingPermission")
    private fun advertisedSocketFactories(device: BluetoothDevice, preferInsecure: Boolean): List<SocketFactoryOption> {
        val uuids = device.uuids
            ?.map { it.uuid }
            ?.distinct()
            ?: emptyList()

        val options = mutableListOf<SocketFactoryOption>()
        for (uuid in uuids) {
            if (preferInsecure) {
                options += SocketFactoryOption("uuid-insecure:$uuid") { target ->
                    target.createInsecureRfcommSocketToServiceRecord(uuid)
                }
                options += SocketFactoryOption("uuid-secure:$uuid") { target ->
                    target.createRfcommSocketToServiceRecord(uuid)
                }
            } else {
                options += SocketFactoryOption("uuid-secure:$uuid") { target ->
                    target.createRfcommSocketToServiceRecord(uuid)
                }
                options += SocketFactoryOption("uuid-insecure:$uuid") { target ->
                    target.createInsecureRfcommSocketToServiceRecord(uuid)
                }
            }
        }
        return options
    }

    private fun prefersInsecureSocket(device: BluetoothDevice): Boolean {
        val name = device.name?.uppercase() ?: return false
        return name.contains("HC-05") ||
            name.contains("HC05") ||
            name.contains("HC-06") ||
            name.contains("HC06") ||
            name.contains("LINVOR")
    }

    @SuppressLint("MissingPermission")
    private fun deviceSummary(device: BluetoothDevice): String {
        val typeName = when (device.type) {
            BluetoothDevice.DEVICE_TYPE_CLASSIC -> "CLASSIC"
            BluetoothDevice.DEVICE_TYPE_LE -> "LE"
            BluetoothDevice.DEVICE_TYPE_DUAL -> "DUAL"
            else -> "UNKNOWN"
        }
        val bondName = when (device.bondState) {
            BluetoothDevice.BOND_BONDED -> "BONDED"
            BluetoothDevice.BOND_BONDING -> "BONDING"
            BluetoothDevice.BOND_NONE -> "NONE"
            else -> "UNKNOWN"
        }
        val uuidCount = device.uuids?.size ?: 0
        return "type=$typeName,bond=$bondName,uuids=$uuidCount"
    }

    private fun bleServicesSummary(services: List<BluetoothGattService>): String {
        return services.joinToString(separator = ";") { service ->
            val chars = service.characteristics.joinToString(",") { characteristic ->
                characteristic.uuid.toString()
            }
            "${service.uuid}[$chars]"
        }
    }

    private fun createChannelOneSocket(device: BluetoothDevice, insecure: Boolean): BluetoothSocket {
        try {
            val methodName = if (insecure) "createInsecureRfcommSocket" else "createRfcommSocket"
            val method = device.javaClass.getMethod(methodName, Int::class.javaPrimitiveType)
            return method.invoke(device, 1) as BluetoothSocket
        } catch (ex: NoSuchMethodException) {
            throw IOException("设备不支持 RFCOMM channel 1 回退", ex)
        } catch (ex: IllegalAccessException) {
            throw IOException("无法访问 RFCOMM 回退接口", ex)
        } catch (ex: InvocationTargetException) {
            throw IOException(ex.targetException?.localizedMessage ?: "RFCOMM 回退失败", ex)
        }
    }

    fun disconnect() {
        thread(name = "bluetooth-disconnect") {
            disconnectInternal(notify = true)
        }
    }

    fun sendLine(command: String) {
        val payload = (command + "\r\n").toByteArray(StandardCharsets.UTF_8)
        if (bleReady && gatt != null) {
            enqueueBleWrite(payload)
            return
        }

        thread(name = "classic-send") {
            try {
                val out = socket?.outputStream ?: return@thread
                out.write(payload)
                out.flush()
            } catch (ex: IOException) {
                disconnectInternal(notify = false)
                onConnectionChanged("发送失败: ${ex.localizedMessage ?: "连接已断开"}", false)
            }
        }
    }

    private fun enqueueBleWrite(payload: ByteArray) {
        val shouldStartWrite = synchronized(bleWriteLock) {
            bleWriteQueue.addLast(payload)
            if (bleWriteInFlight) {
                false
            } else {
                bleWriteInFlight = true
                true
            }
        }

        if (shouldStartWrite) {
            startNextBleWrite()
        }
    }

    private fun startNextBleWrite() {
        val payload = synchronized(bleWriteLock) {
            if (bleWriteQueue.isEmpty()) null else bleWriteQueue.removeFirst()
        }

        if (payload == null) {
            synchronized(bleWriteLock) {
                bleWriteInFlight = false
            }
            return
        }

        try {
            val expectsCallback = writeBle(payload)
            if (!expectsCallback) {
                thread(name = "ble-send-drain") {
                    try {
                        Thread.sleep(BLE_WRITE_NO_RESPONSE_DELAY_MS)
                        onBleWriteFinished()
                    } catch (_: InterruptedException) {
                        handleBleWriteFailure(IOException("BLE 写入被中断"))
                    }
                }
            }
        } catch (ex: IOException) {
            handleBleWriteFailure(ex)
        }
    }

    private fun onBleWriteFinished() {
        val hasMore = synchronized(bleWriteLock) {
            bleWriteQueue.isNotEmpty()
        }

        if (hasMore) {
            startNextBleWrite()
        } else {
            synchronized(bleWriteLock) {
                bleWriteInFlight = false
            }
        }
    }

    private fun handleBleWriteFailure(error: IOException) {
        synchronized(bleWriteLock) {
            bleWriteQueue.clear()
            bleWriteInFlight = false
        }
        disconnectInternal(notify = false)
        onConnectionChanged("发送失败: ${error.localizedMessage ?: "BLE 连接已断开"}", false)
    }

    private fun writeBle(payload: ByteArray): Boolean {
        val activeGatt = gatt ?: throw IOException("BLE 未连接")
        val characteristic = bleWriteCharacteristic ?: throw IOException("BLE 写特征不可用")
        characteristic.writeType = if (
            characteristic.properties and BluetoothGattCharacteristic.PROPERTY_WRITE != 0
        ) {
            BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        } else {
            BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
        }

        val started = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            activeGatt.writeCharacteristic(characteristic, payload, characteristic.writeType) == BluetoothGatt.GATT_SUCCESS
        } else {
            characteristic.value = payload
            activeGatt.writeCharacteristic(characteristic)
        }

        if (!started) {
            throw IOException("BLE 写入启动失败")
        }

        return characteristic.writeType == BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
    }

    private fun startClassicReadLoop(activeSocket: BluetoothSocket) {
        thread(name = "classic-read") {
            try {
                val reader = BufferedReader(InputStreamReader(activeSocket.inputStream, StandardCharsets.UTF_8))
                while (running) {
                    val line = reader.readLine() ?: break
                    onLineReceived(line)
                }
            } catch (_: IOException) {
            } finally {
                disconnectInternal(notify = false)
                onConnectionChanged("连接已断开", false)
            }
        }
    }

    private fun disconnectInternal(notify: Boolean) {
        running = false
        bleReady = false
        bleWriteCharacteristic = null
        bleNotifyCharacteristic = null
        synchronized(bleBufferLock) {
            bleLineBuffer.setLength(0)
        }
        synchronized(bleWriteLock) {
            bleWriteQueue.clear()
            bleWriteInFlight = false
        }

        try {
            socket?.close()
        } catch (_: IOException) {
        }
        socket = null

        val activeGatt = gatt
        gatt = null
        if (activeGatt != null) {
            suppressBleDisconnectEvent = notify
            try {
                activeGatt.disconnect()
            } catch (_: Exception) {
            }
            try {
                activeGatt.close()
            } catch (_: Exception) {
            }
        } else {
            suppressBleDisconnectEvent = false
        }

        if (notify) {
            onConnectionChanged("已断开", false)
        }
    }

    private data class SocketFactoryOption(
        val label: String,
        val create: (BluetoothDevice) -> BluetoothSocket,
    )

    private data class BleUartSelection(
        val writeCharacteristic: BluetoothGattCharacteristic,
        val notifyCharacteristic: BluetoothGattCharacteristic?,
    )

    private class BleConnectState {
        val latch = CountDownLatch(1)
        @Volatile var error: IOException? = null
        @Volatile var completed = false

        fun succeed() {
            if (!completed) {
                completed = true
                latch.countDown()
            }
        }

        fun fail(error: IOException, gatt: BluetoothGatt? = null) {
            if (!completed) {
                this.error = error
                completed = true
                try {
                    gatt?.close()
                } catch (_: Exception) {
                }
                latch.countDown()
            }
        }
    }

    private enum class Transport {
        CLASSIC,
        BLE,
    }

    companion object {
        private const val BLE_CONNECT_TIMEOUT_MS = 12000L
        private const val BLE_SCAN_TIMEOUT_MS = 10000L
        private const val BLE_WRITE_NO_RESPONSE_DELAY_MS = 45L
        private val CLIENT_CONFIG_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805F9B34FB")
        private val SPP_UUID: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")
        private val NORDIC_UART_SERVICE_UUID: UUID = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
        private val NORDIC_UART_WRITE_UUID: UUID = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
        private val NORDIC_UART_NOTIFY_UUID: UUID = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E")
    }
}