package com.example.audiobluetoothmonitor

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.widget.ArrayAdapter
import android.widget.Spinner
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.example.audiobluetoothmonitor.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    private data class DeviceSnapshot(
        val name: String?,
        val type: Int,
        val bondState: Int,
    )

    private lateinit var binding: ActivityMainBinding
    private lateinit var bluetoothService: BluetoothService

    private var availableDevices: List<BluetoothDevice> = emptyList()
    private var suppressUiCallbacks = false
    private var isConnecting = false
    private var isRefreshingDevices = false
    private var selectedDeviceAddress: String? = null
    private var selectedDeviceLabel: String? = null
    private var activeDeviceLabel: String? = null
    private val availableDeviceMap = linkedMapOf<String, BluetoothDevice>()
    private val availableDeviceSnapshots = mutableMapOf<String, DeviceSnapshot>()
    private val pendingProgrammaticSelections = mutableMapOf<Int, Int>()
    private val pendingRemoteValues = mutableMapOf<String, String>()
    private val stagedRemoteValues = mutableMapOf<String, String>()

    private val sourceOptions = listOf("MIC", "LINE")
    private val modeOptions = listOf("TIME", "SPECTRUM", "WATERFALL", "VU")
    private val fftOptions = listOf("64", "128", "256")
    private val windowOptions = listOf("RECT", "HAMM", "HANN", "BLKM")

    private val sourceLabels by lazy { resources.getStringArray(R.array.source_labels).toList() }
    private val modeLabels by lazy { resources.getStringArray(R.array.mode_labels).toList() }
    private val windowLabels by lazy { resources.getStringArray(R.array.window_labels).toList() }
    private val supportedModuleKeywords = listOf("HC-05", "HC05", "HC-06", "HC06", "LINVOR", "BT05", "JDY", "HM10", "AT09")

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { result ->
        if (result.values.all { it }) {
            refreshAvailableDevices()
        } else {
            isConnecting = false
            binding.textConnection.text = "连接状态：权限不足"
            binding.textStatus.text = "请允许蓝牙权限后再刷新蓝牙模块"
            updateConnectButtonState()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        bluetoothService = BluetoothService(
            context = applicationContext,
            onConnectionChanged = { message, connected ->
                runOnUiThread { updateConnectionUi(message, connected) }
            },
            onLineReceived = { line ->
                runOnUiThread { handleIncomingLine(line) }
            }
        )

        setupSelectors()
        setupControls()
        binding.spectrumView.setDisplayMode(modeOptions[binding.spinnerMode.selectedItemPosition])
        updateConnectionUi("未连接", false)

        if (!bluetoothService.isBluetoothSupported()) {
            binding.textConnection.text = "连接状态：设备不支持蓝牙"
            binding.textStatus.text = "当前手机不支持蓝牙"
            binding.buttonConnect.isEnabled = false
            binding.buttonRefresh.isEnabled = false
            return
        }

        ensureBluetoothPermissions { refreshAvailableDevices() }
    }

    private fun setupSelectors() {
        suppressUiCallbacks = true

        binding.spinnerSource.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, sourceLabels)
        binding.spinnerMode.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, modeLabels)
        binding.spinnerFft.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, fftOptions)
        binding.spinnerWindow.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, windowLabels)

        binding.spinnerSource.setSelection(0, false)
        binding.spinnerMode.setSelection(0, false)
        binding.spinnerFft.setSelection(2, false)
        binding.spinnerWindow.setSelection(1, false)

        suppressUiCallbacks = false

        binding.spinnerSource.onItemSelectedListener = SimpleItemSelectedListener { parent, position ->
            if (shouldSendSpinnerCommand(parent, position)) {
                val value = sourceOptions[position]
                stageRemoteValue("SRC", value)
            }
        }
        binding.spinnerMode.onItemSelectedListener = SimpleItemSelectedListener { parent, position ->
            if (shouldSendSpinnerCommand(parent, position)) {
                val value = modeOptions[position]
                stageRemoteValue("MODE", value)
            }
        }
        binding.spinnerFft.onItemSelectedListener = SimpleItemSelectedListener { parent, position ->
            if (shouldSendSpinnerCommand(parent, position)) {
                val value = fftOptions[position]
                stageRemoteValue("FFT", value)
            }
        }
        binding.spinnerWindow.onItemSelectedListener = SimpleItemSelectedListener { parent, position ->
            if (shouldSendSpinnerCommand(parent, position)) {
                val value = windowOptions[position]
                stageRemoteValue("WIN", value)
            }
        }
    }

    private fun setupControls() {
        binding.spinnerDevices.onItemSelectedListener = SimpleItemSelectedListener { _, position ->
            availableDevices.getOrNull(position)?.let { device ->
                selectedDeviceAddress = device.address
                selectedDeviceLabel = deviceLabel(device)
            }
            if (!bluetoothService.isConnected() && !isConnecting) {
                showIdleConnectionState()
            }
        }

        binding.buttonRefresh.setOnClickListener {
            ensureBluetoothPermissions { refreshAvailableDevices() }
        }

        binding.buttonConnect.setOnClickListener {
            if (bluetoothService.isConnected()) {
                bluetoothService.disconnect()
            } else {
                ensureBluetoothPermissions { connectSelectedDevice() }
            }
        }

        binding.buttonApplyThreshold.setOnClickListener {
            sendCommand("TH:${binding.seekThreshold.value.toInt()}")
        }

        binding.buttonUseHardware.setOnClickListener {
            sendCommand("CTRL:LOCAL")
        }

        binding.buttonRequestState.setOnClickListener {
            sendCommand("GET")
        }

        binding.buttonConfirmRemote.setOnClickListener {
            applyRemoteSelections()
        }

        binding.switchDenoise.setOnCheckedChangeListener { _, isChecked ->
            if (suppressUiCallbacks) {
                return@setOnCheckedChangeListener
            }

            if (!bluetoothService.isConnected()) {
                Toast.makeText(this, "请先连接蓝牙设备", Toast.LENGTH_SHORT).show()
                suppressUiCallbacks = true
                binding.switchDenoise.isChecked = !isChecked
                suppressUiCallbacks = false
                return@setOnCheckedChangeListener
            }

            sendCommand(if (isChecked) "DENOISE:ON" else "DENOISE:OFF")
        }

        binding.seekThreshold.addOnChangeListener { _, value, _ ->
            binding.textThresholdControl.text = "阈值: ${value.toInt()}"
        }

        updateRemoteConfirmButtonState()
        updateDenoiseControlState()
    }

    @SuppressLint("MissingPermission")
    private fun refreshAvailableDevices() {
        if (!bluetoothService.isBluetoothEnabled()) {
            bluetoothService.stopDeviceScan()
            availableDevices = emptyList()
            availableDeviceMap.clear()
            availableDeviceSnapshots.clear()
            selectedDeviceAddress = null
            selectedDeviceLabel = null
            binding.spinnerDevices.adapter = ArrayAdapter(
                this,
                android.R.layout.simple_spinner_dropdown_item,
                listOf("请先打开手机系统蓝牙")
            )
            binding.textConnection.text = "连接状态：蓝牙未开启"
            binding.textStatus.text = "打开系统蓝牙后点击刷新蓝牙模块"
            updateConnectButtonState()
            return
        }

        bluetoothService.stopDeviceScan()
        isRefreshingDevices = true
        availableDevices = emptyList()
        availableDeviceMap.clear()
        availableDeviceSnapshots.clear()
        selectedDeviceAddress = null
        selectedDeviceLabel = null
        renderAvailableDevices("正在搜索附近蓝牙模块...")
        if (!bluetoothService.isConnected()) {
            binding.textConnection.text = "连接状态：正在扫描模块"
        }
        binding.textStatus.text = "正在刷新附近蓝牙模块列表..."
        updateConnectButtonState()

        bluetoothService.startDeviceScan(
            onStatusChanged = { message ->
                runOnUiThread {
                    if (!bluetoothService.isConnected()) {
                        binding.textConnection.text = "连接状态：正在扫描模块"
                    }
                    binding.textStatus.text = message
                }
            },
            onDeviceFound = { device ->
                runOnUiThread {
                    upsertAvailableDevice(device)
                }
            },
            onFinished = {
                runOnUiThread {
                    isRefreshingDevices = false
                    finalizeDeviceRefresh()
                }
            },
        )
    }

    @SuppressLint("MissingPermission")
    private fun connectSelectedDevice() {
        val device = availableDevices.getOrNull(binding.spinnerDevices.selectedItemPosition)
        if (device == null) {
            Toast.makeText(this, "请先选择一个蓝牙模块", Toast.LENGTH_SHORT).show()
            return
        }

        isConnecting = true
        selectedDeviceLabel = deviceLabel(device)
        binding.textConnection.text = "连接状态：正在连接"
        binding.textStatus.text = "目标设备：$selectedDeviceLabel"
        updateConnectButtonState()
        bluetoothService.connect(device)
    }

    private fun sendCommand(command: String) {
        if (!bluetoothService.isConnected()) {
            Toast.makeText(this, "请先连接蓝牙设备", Toast.LENGTH_SHORT).show()
            return
        }
        bluetoothService.sendLine(command)
        binding.textStatus.text = "发送: $command"
    }

    private fun stageRemoteValue(key: String, value: String) {
        stagedRemoteValues[key] = value
        binding.textStatus.text = "参数已暂存，点击“确认并应用”后生效"
        updateRemoteConfirmButtonState()
    }

    private fun applyRemoteSelections() {
        if (!bluetoothService.isConnected()) {
            Toast.makeText(this, "请先连接蓝牙设备", Toast.LENGTH_SHORT).show()
            return
        }

        val commands = linkedMapOf(
            "SRC" to sourceOptions[binding.spinnerSource.selectedItemPosition],
            "MODE" to modeOptions[binding.spinnerMode.selectedItemPosition],
            "FFT" to fftOptions[binding.spinnerFft.selectedItemPosition],
            "WIN" to windowOptions[binding.spinnerWindow.selectedItemPosition],
        )

        commands.forEach { (key, value) ->
            pendingRemoteValues[key] = value
            bluetoothService.sendLine("$key:$value")
        }
        stagedRemoteValues.clear()
        binding.textStatus.text = "已发送四项控制参数，等待设备确认"
        updateRemoteConfirmButtonState()
    }

    private fun updateConnectionUi(message: String, connected: Boolean) {
        isConnecting = false
        if (connected) {
            activeDeviceLabel = selectedDeviceLabel ?: message.removePrefix("已连接 ")
            binding.textConnection.text = "连接状态：已连接"
            binding.textStatus.text = "当前设备：${activeDeviceLabel ?: "未知设备"}"
        } else {
            pendingRemoteValues.clear()
            stagedRemoteValues.clear()
            pendingProgrammaticSelections.clear()
            if (message == "系统蓝牙未开启") {
                binding.textConnection.text = "连接状态：蓝牙未开启"
                binding.textStatus.text = "请先打开系统蓝牙"
            } else if (message.startsWith("连接失败")) {
                activeDeviceLabel = null
                binding.textConnection.text = "连接状态：连接失败"
                binding.textStatus.text = message
            } else if (message.startsWith("发送失败")) {
                activeDeviceLabel = null
                binding.textConnection.text = "连接状态：连接已断开"
                binding.textStatus.text = message
            } else if (message == "已断开" || message == "连接已断开") {
                activeDeviceLabel = null
                binding.textConnection.text = "连接状态：已断开"
                binding.textStatus.text = if (availableDevices.isEmpty()) {
                    "请先刷新蓝牙模块列表"
                } else {
                    "当前目标：${selectedDeviceLabel ?: "未选择设备"}"
                }
            } else {
                activeDeviceLabel = null
                binding.textConnection.text = "连接状态：未连接"
                binding.textStatus.text = message
            }
        }
        updateConnectButtonState()
        updateRemoteConfirmButtonState()
        updateDenoiseControlState()
        if (connected) {
            bluetoothService.sendLine("GET")
        }
    }

    private fun showIdleConnectionState() {
        binding.textConnection.text = "连接状态：待连接"
        binding.textStatus.text = if (selectedDeviceLabel == null) {
            if (isRefreshingDevices) {
                "正在搜索附近蓝牙模块"
            } else {
                "请选择一个蓝牙模块"
            }
        } else {
            "当前目标：$selectedDeviceLabel"
        }
        updateConnectButtonState()
    }

    private fun updateConnectButtonState() {
        binding.buttonConnect.text = if (bluetoothService.isConnected()) "断开连接" else "连接"
        binding.buttonConnect.isEnabled = when {
            !bluetoothService.isBluetoothSupported() -> false
            bluetoothService.isConnected() -> true
            !bluetoothService.isBluetoothEnabled() -> false
            isConnecting -> false
            else -> availableDevices.isNotEmpty()
        }
    }

    @SuppressLint("MissingPermission")
    private fun deviceLabel(device: BluetoothDevice): String {
        val name = device.name?.takeIf { it.isNotBlank() } ?: "未命名设备"
        val type = when (device.type) {
            BluetoothDevice.DEVICE_TYPE_LE -> "BLE"
            BluetoothDevice.DEVICE_TYPE_CLASSIC -> "经典蓝牙"
            BluetoothDevice.DEVICE_TYPE_DUAL -> "双模蓝牙"
            else -> "蓝牙设备"
        }
        val bond = when (device.bondState) {
            BluetoothDevice.BOND_BONDED -> "已配对"
            BluetoothDevice.BOND_BONDING -> "配对中"
            else -> "未配对"
        }
        return "$name · $type · $bond (${device.address})"
    }

    private fun upsertAvailableDevice(device: BluetoothDevice) {
        if (!isSupportedModuleDevice(device)) {
            return
        }

        val nextSnapshot = DeviceSnapshot(
            name = device.name,
            type = device.type,
            bondState = device.bondState,
        )
        if (availableDeviceSnapshots[device.address] == nextSnapshot) {
            return
        }

        availableDeviceMap[device.address] = device
        availableDeviceSnapshots[device.address] = nextSnapshot
        availableDevices = availableDeviceMap.values
            .sortedWith(compareBy({ it.name ?: "" }, { it.address }))
            .toList()
        renderAvailableDevices()
    }

    private fun renderAvailableDevices(emptyMessage: String = "未发现附近蓝牙模块") {
        val labels = if (availableDevices.isEmpty()) {
            listOf(emptyMessage)
        } else {
            availableDevices.map(::deviceLabel)
        }
        binding.spinnerDevices.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, labels)

        if (availableDevices.isEmpty()) {
            selectedDeviceAddress = null
            selectedDeviceLabel = null
        } else {
            val selectedIndex = selectedDeviceAddress
                ?.let { address -> availableDevices.indexOfFirst { it.address == address } }
                ?.takeIf { it >= 0 }
                ?: 0
            binding.spinnerDevices.setSelection(selectedIndex, false)
            val selectedDevice = availableDevices[selectedIndex]
            selectedDeviceAddress = selectedDevice.address
            selectedDeviceLabel = deviceLabel(selectedDevice)
        }

        if (!bluetoothService.isConnected() && !isConnecting) {
            showIdleConnectionState()
        }
        updateConnectButtonState()
    }

    private fun finalizeDeviceRefresh() {
        if (availableDevices.isEmpty()) {
            binding.textConnection.text = "连接状态：未发现模块"
            binding.textStatus.text = "未发现 HC-05 类模块或候选串口设备，请确认模块已上电、可发现，或已在系统蓝牙中完成配对"
        } else {
            if (!bluetoothService.isConnected() && !isConnecting) {
                showIdleConnectionState()
            }
            binding.textStatus.text = "已发现 ${availableDevices.size} 个蓝牙模块或候选串口设备"
        }
        updateConnectButtonState()
    }

    private fun isSupportedModuleDevice(device: BluetoothDevice): Boolean {
        val normalizedName = device.name
            ?.uppercase()
            ?.replace(" ", "")
            ?: ""

        if (supportedModuleKeywords.any { keyword ->
                normalizedName.contains(keyword)
            }) {
            return true
        }

        val isClassicCandidate = when (device.type) {
            BluetoothDevice.DEVICE_TYPE_CLASSIC,
            BluetoothDevice.DEVICE_TYPE_DUAL,
            BluetoothDevice.DEVICE_TYPE_UNKNOWN -> true
            else -> false
        }

        if (!isClassicCandidate) {
            return false
        }

        return device.bondState == BluetoothDevice.BOND_BONDED || normalizedName.isBlank()
    }

    private fun handleIncomingLine(line: String) {
        when {
            line.startsWith("STAT:") -> applyStatusFrame(line.removePrefix("STAT:"))
            line.startsWith("FFT:") -> applySpectrumFrame(line.removePrefix("FFT:"))
            line.startsWith("WAV:") -> applyWaveformFrame(line.removePrefix("WAV:"))
            line.startsWith("VU:") -> applyVuFrame(line.removePrefix("VU:"))
            line.startsWith("ACK:") -> binding.textStatus.text = "设备应答: ${line.removePrefix("ACK:")}"
            line.startsWith("ERR:") -> binding.textStatus.text = "设备错误: ${line.removePrefix("ERR:")}"
            line.startsWith("HELLO:") -> binding.textStatus.text = line
            else -> binding.textStatus.text = line
        }
    }

    private fun applyStatusFrame(payload: String) {
        val statusMap = payload
            .split(',')
            .mapNotNull { item ->
                val parts = item.split('=', limit = 2)
                if (parts.size == 2) parts[0] to parts[1] else null
            }
            .toMap()

        binding.textSourceValue.text = displaySource(statusMap["SRC"])
        binding.textModeValue.text = displayMode(statusMap["MODE"])
        binding.textFftValue.text = statusMap["FFT"] ?: "-"
        binding.textWindowValue.text = displayWindow(statusMap["WIN"])
        binding.textRmsValue.text = statusMap["RMS"] ?: "-"
        binding.textThresholdValue.text = statusMap["TH"] ?: "-"
        binding.textWidthValue.text = statusMap["WIDTH"]?.let { "${it}x" } ?: "-"
        binding.textDenoiseValue.text = when (statusMap["DENOISE"]) {
            "ON" -> "已开启"
            "OFF" -> "已关闭"
            else -> "-"
        }
        binding.textControlValue.text = buildControlSummary(statusMap["CFG"], statusMap["DENOISE"])
        statusMap["MODE"]?.let { binding.spectrumView.setDisplayMode(it) }

        val alarmActive = statusMap["ALARM"] == "1"
        binding.textAlarmValue.text = if (alarmActive) "报警中" else "正常"
        binding.textAlarmValue.setTextColor(
            ContextCompat.getColor(this, if (alarmActive) android.R.color.holo_red_light else android.R.color.holo_green_light)
        )

        suppressUiCallbacks = true
        syncRemoteSelection("SRC", statusMap["SRC"], binding.spinnerSource, sourceOptions)
        syncRemoteSelection("MODE", statusMap["MODE"], binding.spinnerMode, modeOptions)
        syncRemoteSelection("FFT", statusMap["FFT"], binding.spinnerFft, fftOptions)
        syncRemoteSelection("WIN", statusMap["WIN"], binding.spinnerWindow, windowOptions)
        statusMap["DENOISE"]?.let { binding.switchDenoise.isChecked = it == "ON" }
        statusMap["TH"]?.toIntOrNull()?.let {
            binding.seekThreshold.value = it.coerceIn(0, 128).toFloat()
            binding.textThresholdControl.text = "阈值: ${binding.seekThreshold.value.toInt()}"
        }
        suppressUiCallbacks = false
    }

    private fun applySpectrumFrame(payload: String) {
        val values = payload.split(',')
            .mapNotNull { it.toIntOrNull() }
            .toIntArray()
        binding.spectrumView.setSpectrum(values)
    }

    private fun applyWaveformFrame(payload: String) {
        val values = payload.split(',')
            .mapNotNull { it.toIntOrNull() }
            .toIntArray()
        binding.spectrumView.setWaveform(values)
    }

    private fun applyVuFrame(payload: String) {
        val values = payload.split(',')
            .mapNotNull { it.toIntOrNull() }
        if (values.isNotEmpty()) {
            val rms = values[0]
            val peak = values.getOrElse(1) { rms }
            binding.spectrumView.setVu(rms, peak)
        }
    }

    private fun syncSelection(spinner: android.widget.Spinner, options: List<String>, value: String) {
        val index = options.indexOf(value)
        if (index >= 0 && spinner.selectedItemPosition != index) {
            pendingProgrammaticSelections[spinner.id] = index
            spinner.setSelection(index, false)
        }
    }

    private fun displaySource(value: String?): String {
        return when (value) {
            "MIC" -> sourceLabels.getOrElse(0) { "MIC" }
            "LINE" -> sourceLabels.getOrElse(1) { "LINE" }
            else -> value ?: "-"
        }
    }

    private fun displayMode(value: String?): String {
        return when (value) {
            "TIME" -> modeLabels.getOrElse(0) { "TIME" }
            "SPECTRUM" -> modeLabels.getOrElse(1) { "SPECTRUM" }
            "WATERFALL" -> modeLabels.getOrElse(2) { "WATERFALL" }
            "VU" -> modeLabels.getOrElse(3) { "VU" }
            else -> value ?: "-"
        }
    }

    private fun displayWindow(value: String?): String {
        return when (value) {
            "RECT" -> windowLabels.getOrElse(0) { "RECT" }
            "HAMM" -> windowLabels.getOrElse(1) { "HAMM" }
            "HANN" -> windowLabels.getOrElse(2) { "HANN" }
            "BLKM" -> windowLabels.getOrElse(3) { "BLKM" }
            else -> value ?: "-"
        }
    }

    private fun displayControlSource(value: String?): String {
        return when (value) {
            "APP" -> "手机应用"
            "DIP" -> "硬件拨码开关"
            "POT" -> "板上电位器旋钮"
            else -> value ?: "-"
        }
    }

    private fun buildControlSummary(fftSource: String?, denoiseState: String?): String {
        val denoiseSummary = when (denoiseState) {
            "ON" -> "增强降噪由手机应用开关，当前已开启"
            "OFF" -> "增强降噪由手机应用开关，当前已关闭"
            else -> "增强降噪由手机应用开关"
        }
        return "FFT 参数由${displayControlSource(fftSource)}控制，报警阈值由手机应用控制，显示宽度由板上电位器调节，电源噪声抑制始终开启，$denoiseSummary"
    }

    private fun syncRemoteSelection(key: String, value: String?, spinner: Spinner, options: List<String>) {
        val remoteValue = value ?: return
        if (stagedRemoteValues.containsKey(key)) {
            return
        }
        val pendingValue = pendingRemoteValues[key]
        if (pendingValue != null) {
            if (pendingValue == remoteValue) {
                pendingRemoteValues.remove(key)
                updateRemoteConfirmButtonState()
            } else {
                return
            }
        }
        syncSelection(spinner, options, remoteValue)
    }

    private fun shouldSendSpinnerCommand(parent: android.widget.AdapterView<*>?, position: Int): Boolean {
        if (suppressUiCallbacks) {
            return false
        }

        val spinnerId = parent?.id ?: return true
        val pendingPosition = pendingProgrammaticSelections[spinnerId]
        if (pendingPosition == position) {
            pendingProgrammaticSelections.remove(spinnerId)
            return false
        }

        return true
    }

    private fun updateRemoteConfirmButtonState() {
        binding.buttonConfirmRemote.isEnabled = bluetoothService.isConnected() && stagedRemoteValues.isNotEmpty()
        binding.buttonConfirmRemote.alpha = if (binding.buttonConfirmRemote.isEnabled) 1f else 0.72f
    }

    private fun updateDenoiseControlState() {
        binding.switchDenoise.isEnabled = bluetoothService.isConnected()
        binding.switchDenoise.alpha = if (binding.switchDenoise.isEnabled) 1f else 0.72f
    }

    private fun ensureBluetoothPermissions(onGranted: () -> Unit) {
        val permissions = requiredPermissions()
        val denied = permissions.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (denied.isEmpty()) {
            onGranted()
        } else {
            permissionLauncher.launch(denied.toTypedArray())
        }
    }

    private fun requiredPermissions(): Array<String> {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(
                Manifest.permission.BLUETOOTH_CONNECT,
                Manifest.permission.BLUETOOTH_SCAN,
            )
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        bluetoothService.stopDeviceScan()
        bluetoothService.disconnect()
    }
}