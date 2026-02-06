package com.cheng.mobile

import android.content.Context
import android.util.AttributeSet
import android.text.InputType
import android.view.Choreographer
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection

class ChengSurfaceView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : SurfaceView(context, attrs),
    View.OnTouchListener,
    SurfaceHolder.Callback,
    Choreographer.FrameCallback {
    private var windowId = 1
    private var lastX = 0.0f
    private var lastY = 0.0f
    private var hasLast = false
    private var running = false
    private var started = false

    init {
        isFocusable = true
        isFocusableInTouchMode = true
        setOnTouchListener(this)
        holder.addCallback(this)
        requestFocus()
    }

    fun startFrameTick() {
        if (running) {
            return
        }
        running = true
        Choreographer.getInstance().postFrameCallback(this)
    }

    fun stopFrameTick() {
        running = false
    }

    override fun doFrame(frameTimeNanos: Long) {
        if (!running) {
            return
        }
        ChengNative.onFrame()
        Choreographer.getInstance().postFrameCallback(this)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        val frame = holder.surfaceFrame
        ChengNative.onSurface(holder.surface, frame.width(), frame.height())
        if (!started) {
            started = true
            ChengNative.start()
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        ChengNative.onSurface(holder.surface, width, height)
        if (!started) {
            started = true
            ChengNative.start()
        }
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        ChengNative.onSurface(null, 0, 0)
    }

    override fun onTouch(v: View, ev: MotionEvent): Boolean {
        val action = ev.actionMasked
        val x = ev.x
        val y = ev.y
        var dx = 0.0f
        var dy = 0.0f
        if (action == MotionEvent.ACTION_MOVE && hasLast) {
            dx = x - lastX
            dy = y - lastY
        } else if (action == MotionEvent.ACTION_SCROLL) {
            dx = ev.getAxisValue(MotionEvent.AXIS_HSCROLL)
            dy = ev.getAxisValue(MotionEvent.AXIS_VSCROLL)
        }
        lastX = x
        lastY = y
        hasLast = action != MotionEvent.ACTION_UP && action != MotionEvent.ACTION_CANCEL
        val button = ev.buttonState
        ChengNative.onTouch(windowId, action, x, y, dx, dy, button)
        return true
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        ChengNative.onKey(windowId, keyCode, true, event.repeatCount > 0)
        val unicode = event.unicodeChar
        if (unicode != 0 && !event.isAltPressed && !event.isCtrlPressed) {
            ChengNative.onText(windowId, unicode.toChar().toString())
        }
        return true
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        ChengNative.onKey(windowId, keyCode, false, event.repeatCount > 0)
        return true
    }

    override fun onCheckIsTextEditor(): Boolean {
        return true
    }

    override fun onCreateInputConnection(outAttrs: EditorInfo): InputConnection {
        outAttrs.imeOptions = EditorInfo.IME_ACTION_NONE
        outAttrs.inputType = InputType.TYPE_CLASS_TEXT
        return ChengInputConnection(this)
    }

    private inner class ChengInputConnection(targetView: View) : BaseInputConnection(targetView, false) {
        override fun commitText(text: CharSequence?, newCursorPosition: Int): Boolean {
            if (text != null && text.isNotEmpty()) {
                ChengNative.onText(windowId, text.toString())
            }
            return super.commitText(text, newCursorPosition)
        }

        override fun deleteSurroundingText(beforeLength: Int, afterLength: Int): Boolean {
            if (beforeLength > 0) {
                ChengNative.onKey(windowId, KeyEvent.KEYCODE_DEL, true, false)
                ChengNative.onKey(windowId, KeyEvent.KEYCODE_DEL, false, false)
            }
            return super.deleteSurroundingText(beforeLength, afterLength)
        }
    }
}

