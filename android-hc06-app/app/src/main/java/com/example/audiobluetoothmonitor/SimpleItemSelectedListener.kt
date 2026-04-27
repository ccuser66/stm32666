package com.example.audiobluetoothmonitor

import android.view.View
import android.widget.AdapterView

class SimpleItemSelectedListener(
    private val onSelected: (AdapterView<*>?, Int) -> Unit,
) : AdapterView.OnItemSelectedListener {
    override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
        onSelected(parent, position)
    }

    override fun onNothingSelected(parent: AdapterView<*>?) = Unit
}