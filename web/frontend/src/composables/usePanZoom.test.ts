import { describe, expect, it } from 'vitest'
import { usePanZoom } from './usePanZoom'

function makeWheelEvent(deltaY: number): WheelEvent {
  const element = {
    getBoundingClientRect: () => ({ left: 0, top: 0 }),
  } as unknown as HTMLElement
  return {
    deltaY,
    clientX: 50,
    clientY: 30,
    currentTarget: element,
    preventDefault: () => undefined,
  } as unknown as WheelEvent
}

describe('usePanZoom', () => {
  it('should zoom in and out around cursor', () => {
    const panZoom = usePanZoom()
    panZoom.handleWheel(makeWheelEvent(-100))
    expect(panZoom.scale.value).toBeGreaterThan(1)

    panZoom.handleWheel(makeWheelEvent(100))
    expect(panZoom.scale.value).toBeGreaterThan(0)
  })

  it('should drag and reset view', () => {
    const panZoom = usePanZoom()
    panZoom.handleMouseDown({ button: 0, clientX: 10, clientY: 10 } as MouseEvent)
    panZoom.handleMouseMove({ clientX: 30, clientY: 35 } as MouseEvent)
    panZoom.handleMouseUp()

    expect(panZoom.translateX.value).toBe(20)
    expect(panZoom.translateY.value).toBe(25)
    expect(panZoom.dragging.value).toBe(false)

    panZoom.resetView()
    expect(panZoom.translateX.value).toBe(0)
    expect(panZoom.translateY.value).toBe(0)
    expect(panZoom.scale.value).toBe(1)
  })
})
