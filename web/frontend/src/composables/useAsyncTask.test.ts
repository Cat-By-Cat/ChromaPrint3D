import { afterEach, describe, expect, it, vi } from 'vitest'
import { createApp, defineComponent, h } from 'vue'
import { useAsyncTask } from './useAsyncTask'

type TaskStatus = {
  status: string
  error?: string | null
}

function mountTask(
  submitFn: () => Promise<{ task_id: string }>,
  pollFn: (id: string) => Promise<TaskStatus>,
  pollInterval: number,
) {
  let task!: ReturnType<typeof useAsyncTask<TaskStatus>>
  const container = document.createElement('div')
  const app = createApp(
    defineComponent({
      setup() {
        task = useAsyncTask<TaskStatus>(submitFn, pollFn, { pollInterval })
        return () => h('div')
      },
    }),
  )
  app.mount(container)
  return {
    task,
    unmount: () => app.unmount(),
  }
}

describe('useAsyncTask polling fallback', () => {
  afterEach(() => {
    vi.useRealTimers()
  })

  it('在轮询返回 401 时应立即停止并暴露错误', async () => {
    vi.useFakeTimers()
    const submitFn = vi.fn().mockResolvedValue({ task_id: 'task-1' })
    const pollFn = vi.fn().mockRejectedValue(new Error('HTTP 401'))
    const harness = mountTask(submitFn, pollFn, 100)
    try {
      await harness.task.submit()
      expect(harness.task.loading.value).toBe(true)

      await vi.advanceTimersByTimeAsync(100)

      expect(harness.task.loading.value).toBe(false)
      expect(harness.task.error.value).toBe('HTTP 401')
    } finally {
      harness.unmount()
    }
  })

  it('在连续轮询失败超阈值后应停止并提示重试', async () => {
    vi.useFakeTimers()
    const submitFn = vi.fn().mockResolvedValue({ task_id: 'task-2' })
    const pollFn = vi.fn().mockRejectedValue(new Error('network timeout'))
    const harness = mountTask(submitFn, pollFn, 50)
    try {
      await harness.task.submit()
      expect(harness.task.loading.value).toBe(true)

      await vi.advanceTimersByTimeAsync(50 * 10)

      expect(harness.task.loading.value).toBe(false)
      expect(harness.task.error.value).toContain('任务状态轮询失败，请重试')
    } finally {
      harness.unmount()
    }
  })
})
