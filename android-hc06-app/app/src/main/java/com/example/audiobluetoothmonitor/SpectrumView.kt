package com.example.audiobluetoothmonitor

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.util.AttributeSet
import android.view.View
import java.util.ArrayDeque
import kotlin.math.max
import kotlin.math.min

class SpectrumView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : View(context, attrs) {

    private var displayMode = DisplayMode.TIME
    private var spectrum: IntArray = intArrayOf()
    private var waveform: IntArray = intArrayOf()
    private var vuRms = 0
    private var vuPeak = 0
    private var spectrumScaleMax = 96f
    private val waterfallHistory = ArrayDeque<IntArray>()

    private val backgroundPaint = Paint().apply { color = Color.parseColor("#10161F") }
    private val gridPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#243243")
        strokeWidth = 1f
    }
    private val spectrumPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#26C6DA")
    }
    private val waveformPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#66E26F")
        strokeWidth = 3f
        style = Paint.Style.STROKE
    }
    private val accentPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#FFB74D")
        strokeWidth = 2f
    }
    private val vuFillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#26C6DA")
        style = Paint.Style.FILL
    }
    private val vuPeakPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#FF7043")
        strokeWidth = 4f
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#D7E3F4")
        textSize = 32f
        textAlign = Paint.Align.CENTER
    }
    private val labelPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#6FA8DC")
        textSize = 24f
        textAlign = Paint.Align.CENTER
    }
    private val modePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#8DB6D9")
        textSize = 24f
        textAlign = Paint.Align.LEFT
    }

    fun setDisplayMode(mode: String) {
        val nextMode = when (mode.uppercase()) {
            "TIME" -> DisplayMode.TIME
            "SPECTRUM" -> DisplayMode.SPECTRUM
            "WATERFALL" -> DisplayMode.WATERFALL
            "VU" -> DisplayMode.VU
            else -> displayMode
        }
        if (nextMode != displayMode) {
            displayMode = nextMode
            invalidate()
        }
    }

    fun setSpectrum(values: IntArray) {
        spectrum = values.copyOf()
        updateSpectrumScale(spectrum)
        appendWaterfallRow(spectrum)
        invalidate()
    }

    fun setWaveform(values: IntArray) {
        waveform = values.copyOf()
        invalidate()
    }

    fun setVu(rms: Int, peak: Int) {
        vuRms = rms.coerceIn(0, 128)
        vuPeak = peak.coerceIn(0, 128)
        invalidate()
    }

    private fun updateSpectrumScale(values: IntArray) {
        val currentMax = max(1, values.maxOrNull() ?: 1).toFloat()
        spectrumScaleMax = max(SPECTRUM_SCALE_FLOOR, max(currentMax, spectrumScaleMax * 0.92f))
    }

    private fun appendWaterfallRow(values: IntArray) {
        if (values.isEmpty()) {
            return
        }

        val row = compressSpectrum(values, WATERFALL_COLUMNS)
        if (waterfallHistory.size >= WATERFALL_HISTORY_LIMIT) {
            waterfallHistory.removeFirst()
        }
        waterfallHistory.addLast(row)
    }

    private fun compressSpectrum(values: IntArray, columns: Int): IntArray {
        if (values.isEmpty() || columns <= 0) {
            return intArrayOf()
        }

        val result = IntArray(columns)
        for (index in 0 until columns) {
            val start = index * values.size / columns
            val end = max(start + 1, (index + 1) * values.size / columns)
            var sum = 0L
            for (sampleIndex in start until min(end, values.size)) {
                sum += values[sampleIndex].toLong()
            }
            result[index] = (sum / max(1, end - start)).toInt()
        }
        return result
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)

        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), backgroundPaint)

        val left = paddingLeft.toFloat() + 12f
        val right = width.toFloat() - paddingRight.toFloat() - 12f
        val bottom = height.toFloat() - paddingBottom.toFloat() - 36f
        val titleBaseline = paddingTop.toFloat() + 16f + modePaint.textHeight()
        val top = titleBaseline + 14f

        canvas.drawText(displayMode.title, left, titleBaseline, modePaint)

        for (index in 0..4) {
            val y = top + (bottom - top) * index / 4f
            canvas.drawLine(left, y, right, y, gridPaint)
        }

        when (displayMode) {
            DisplayMode.TIME -> drawTimeMode(canvas, left, top, right, bottom)
            DisplayMode.SPECTRUM -> drawSpectrumMode(canvas, left, top, right, bottom)
            DisplayMode.WATERFALL -> drawWaterfallMode(canvas, left, top, right, bottom)
            DisplayMode.VU -> drawVuMode(canvas, left, top, right, bottom)
        }
    }

    private fun drawTimeMode(canvas: Canvas, left: Float, top: Float, right: Float, bottom: Float) {
        canvas.drawText("0", left + 16f, height.toFloat() - 8f, labelPaint)
        canvas.drawText("128", (left + right) / 2f, height.toFloat() - 8f, labelPaint)
        canvas.drawText("255", right - 18f, height.toFloat() - 8f, labelPaint)
        val midY = (top + bottom) / 2f
        canvas.drawLine(left, midY, right, midY, accentPaint)

        if (waveform.isEmpty()) {
            canvas.drawText("等待波形数据", width / 2f, height / 2f, textPaint)
            return
        }

        val count = waveform.size
        val stepX = if (count > 1) (right - left) / (count - 1).toFloat() else 0f
        var prevX = left
        var prevY = mapWaveY(waveform.first(), top, bottom)
        for (index in 1 until count) {
            val x = left + index * stepX
            val y = mapWaveY(waveform[index], top, bottom)
            canvas.drawLine(prevX, prevY, x, y, waveformPaint)
            prevX = x
            prevY = y
        }
    }

    private fun drawSpectrumMode(canvas: Canvas, left: Float, top: Float, right: Float, bottom: Float) {
        drawSpectrumLabels(canvas, left, right)

        if (spectrum.isEmpty()) {
            canvas.drawText("等待 FFT 数据", width / 2f, height / 2f, textPaint)
            return
        }

        val bars = compressSpectrum(spectrum, SPECTRUM_BAR_COUNT)
        val contentWidth = right - left
        val barWidth = contentWidth / bars.size.toFloat()
        for (index in bars.indices) {
            val value = bars[index].coerceAtLeast(0).toFloat()
            val ratio = (value / spectrumScaleMax).coerceIn(0f, 1f)
            val barHeight = (bottom - top) * ratio
            val barLeft = left + index * barWidth
            val barRight = barLeft + barWidth * 0.82f
            val barTop = bottom - barHeight
            canvas.drawRect(barLeft, barTop, barRight, bottom, spectrumPaint)
        }
    }

    private fun drawWaterfallMode(canvas: Canvas, left: Float, top: Float, right: Float, bottom: Float) {
        drawSpectrumLabels(canvas, left, right)

        if (waterfallHistory.isEmpty()) {
            canvas.drawText("等待瀑布图数据", width / 2f, height / 2f, textPaint)
            return
        }

        val rows = waterfallHistory.toList()
        val rowCount = rows.size
        val rowHeight = (bottom - top) / max(1, rowCount).toFloat()
        for (rowIndex in rows.indices) {
            val row = rows[rowCount - 1 - rowIndex]
            val y0 = bottom - (rowIndex + 1) * rowHeight
            val y1 = bottom - rowIndex * rowHeight
            val cellWidth = (right - left) / row.size.toFloat()
            for (column in row.indices) {
                val ratio = (row[column].toFloat() / spectrumScaleMax).coerceIn(0f, 1f)
                val paint = Paint().apply { color = colorForIntensity(ratio) }
                val x0 = left + column * cellWidth
                val x1 = x0 + cellWidth + 1f
                canvas.drawRect(x0, y0, x1, y1, paint)
            }
        }
    }

    private fun drawVuMode(canvas: Canvas, left: Float, top: Float, right: Float, bottom: Float) {
        val meterTop = top + 28f
        val meterBottom = bottom - 24f
        val meterLeft = left + 24f
        val meterRight = right - 24f
        val meterHeight = meterBottom - meterTop

        canvas.drawRect(meterLeft, meterTop, meterRight, meterBottom, gridPaint)
        for (index in 0..4) {
            val y = meterBottom - meterHeight * index / 4f
            canvas.drawLine(meterLeft, y, meterRight, y, gridPaint)
        }

        val rmsRatio = (vuRms / 128f).coerceIn(0f, 1f)
        val peakRatio = (vuPeak / 128f).coerceIn(0f, 1f)
        val fillTop = meterBottom - meterHeight * rmsRatio
        canvas.drawRect(meterLeft, fillTop, meterRight, meterBottom, vuFillPaint)

        val peakY = meterBottom - meterHeight * peakRatio
        canvas.drawLine(meterLeft, peakY, meterRight, peakY, vuPeakPaint)

        canvas.drawText("RMS $vuRms", width / 2f, top + 18f, textPaint)
        canvas.drawText("Peak $vuPeak", width / 2f, bottom + 2f, labelPaint)
    }

    private fun drawSpectrumLabels(canvas: Canvas, left: Float, right: Float) {
        canvas.drawText("0 Hz", left + 20f, height.toFloat() - 8f, labelPaint)
        canvas.drawText("2 kHz", (left + right) / 2f, height.toFloat() - 8f, labelPaint)
        canvas.drawText("4 kHz", right - 20f, height.toFloat() - 8f, labelPaint)
    }

    private fun mapWaveY(sample: Int, top: Float, bottom: Float): Float {
        val normalized = sample.coerceIn(0, 255) / 255f
        return bottom - normalized * (bottom - top)
    }

    private fun colorForIntensity(ratio: Float): Int {
        val clamped = ratio.coerceIn(0f, 1f)
        return when {
            clamped < 0.25f -> Color.rgb(18, 26, 38 + (clamped * 120f).toInt())
            clamped < 0.5f -> Color.rgb(24, 120, 160 + ((clamped - 0.25f) * 180f).toInt())
            clamped < 0.75f -> Color.rgb(40 + ((clamped - 0.5f) * 400f).toInt(), 210, 120)
            else -> Color.rgb(255, 200 - ((clamped - 0.75f) * 240f).toInt(), 80)
        }
    }

    private fun Paint.textHeight(): Float {
        return fontMetrics.descent - fontMetrics.ascent
    }

    private enum class DisplayMode(val title: String) {
        TIME("时域波形"),
        SPECTRUM("频域频谱"),
        WATERFALL("瀑布图"),
        VU("VU 电平"),
    }

    companion object {
        private const val SPECTRUM_BAR_COUNT = 36
        private const val SPECTRUM_SCALE_FLOOR = 48f
        private const val WATERFALL_COLUMNS = 40
        private const val WATERFALL_HISTORY_LIMIT = 60
    }
}