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
import android.view.inputmethod.InputMethodManager

class ChengSurfaceView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : SurfaceView(context, attrs),
    View.OnTouchListener,
    SurfaceHolder.Callback,
    Choreographer.FrameCallback {
    private var windowId = 1
    private var activePointerId = -1
    private var lastX = 0.0f
    private var lastY = 0.0f
    private var hasLast = false
    private var running = false
    private var hasSurface = false

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

    fun setImeVisible(visible: Boolean) {
        post {
            val imm = context.getSystemService(Context.INPUT_METHOD_SERVICE) as? InputMethodManager ?: return@post
            if (visible) {
                requestFocus()
                imm.showSoftInput(this, InputMethodManager.SHOW_IMPLICIT)
            } else {
                imm.hideSoftInputFromWindow(windowToken, 0)
            }
        }
    }

    override fun doFrame(frameTimeNanos: Long) {
        if (!running) {
            return
        }
        if (hasSurface && ChengNative.isStarted()) {
            ChengNative.onFrame()
        }
        Choreographer.getInstance().postFrameCallback(this)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        val surface = holder.surface
        val frame = holder.surfaceFrame
        hasSurface = true
        ChengNative.onSurface(surface, frame.width(), frame.height())
        ChengNative.startOnce()
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        hasSurface = true
        ChengNative.onSurface(holder.surface, width, height)
        ChengNative.startOnce()
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        hasSurface = false
        ChengNative.onSurface(null, 0, 0)
    }

    override fun onTouch(v: View, ev: MotionEvent): Boolean {
        val action = ev.actionMasked
        val eventTimeMs = ev.eventTime
        val actionIndex = ev.actionIndex

        if (action == MotionEvent.ACTION_DOWN) {
            activePointerId = ev.getPointerId(0)
        } else if (action == MotionEvent.ACTION_POINTER_DOWN && activePointerId < 0) {
            activePointerId = ev.getPointerId(actionIndex)
        }

        val pointerId = when (action) {
            MotionEvent.ACTION_MOVE -> if (activePointerId >= 0) activePointerId else ev.getPointerId(0)
            MotionEvent.ACTION_CANCEL -> if (activePointerId >= 0) activePointerId else 0
            else -> ev.getPointerId(actionIndex)
        }

        val pointerIndex = when (action) {
            MotionEvent.ACTION_MOVE -> {
                val idx = if (activePointerId >= 0) ev.findPointerIndex(activePointerId) else 0
                if (idx >= 0) idx else 0
            }
            else -> actionIndex
        }

        val x = ev.getX(pointerIndex)
        val y = ev.getY(pointerIndex)
        var dx = 0.0f
        var dy = 0.0f
        if (action == MotionEvent.ACTION_MOVE && hasLast) {
            dx = x - lastX
            dy = y - lastY
        } else if (action == MotionEvent.ACTION_SCROLL) {
            dx = ev.getAxisValue(MotionEvent.AXIS_HSCROLL, pointerIndex)
            dy = ev.getAxisValue(MotionEvent.AXIS_VSCROLL, pointerIndex)
        }
        lastX = x
        lastY = y
        hasLast = action != MotionEvent.ACTION_UP && action != MotionEvent.ACTION_CANCEL
        if (action == MotionEvent.ACTION_POINTER_UP && activePointerId >= 0) {
            val upId = ev.getPointerId(actionIndex)
            if (upId == activePointerId) {
                val newIndex = if (actionIndex == 0) 1 else 0
                if (newIndex < ev.pointerCount) {
                    activePointerId = ev.getPointerId(newIndex)
                    lastX = ev.getX(newIndex)
                    lastY = ev.getY(newIndex)
                    hasLast = true
                } else {
                    activePointerId = -1
                    hasLast = false
                }
            }
        } else if (!hasLast) {
            activePointerId = -1
        }
        val button = ev.buttonState
        ChengNative.onTouch(windowId, action, pointerId, eventTimeMs, x, y, dx, dy, button)
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
