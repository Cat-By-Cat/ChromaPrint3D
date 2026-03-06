import { describe, expect, it } from 'vitest'
import { usePanZoomGroups } from './usePanZoomGroups'
import type { PanZoomController } from './usePanZoom'

function drag(controller: PanZoomController): void {
  controller.handleMouseDown({ button: 0, clientX: 10, clientY: 10 } as MouseEvent)
  controller.handleMouseMove({ clientX: 40, clientY: 35 } as MouseEvent)
  controller.handleMouseUp()
}

describe('usePanZoomGroups', () => {
  it('同组视图应复用同一个控制器', () => {
    const groups = usePanZoomGroups({
      left: 'A',
      right: 'A',
    })

    const leftController = groups.controllerFor('left')
    const rightController = groups.controllerFor('right')

    expect(leftController).toBe(rightController)
  })

  it('独立分组后应使用不同控制器并可统一重置', () => {
    const groups = usePanZoomGroups({
      left: 'A',
      right: 'A',
    })

    const leftController = groups.controllerFor('left')
    groups.setViewGroup('right', 'self:right')
    const rightController = groups.controllerFor('right')

    expect(rightController).not.toBe(leftController)

    drag(leftController)
    drag(rightController)
    expect(leftController.translateX.value).not.toBe(0)
    expect(rightController.translateX.value).not.toBe(0)

    groups.resetAll()
    expect(leftController.translateX.value).toBe(0)
    expect(leftController.translateY.value).toBe(0)
    expect(rightController.translateX.value).toBe(0)
    expect(rightController.translateY.value).toBe(0)
  })

  it('空分组值应自动回退到视图自身', () => {
    const groups = usePanZoomGroups({
      current: 'A',
    })

    groups.setViewGroup('current', '')
    expect(groups.getViewGroup('current')).toBe('current')
  })
})
