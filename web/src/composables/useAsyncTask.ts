import { ref, computed, onUnmounted, type Ref, type ComputedRef } from 'vue'

export interface AsyncTaskOptions<TStatus> {
  pollInterval?: number
  onCompleted?: (status: TStatus) => void
  onFailed?: (status: TStatus) => void
}

export interface AsyncTaskReturn<TStatus> {
  taskId: Ref<string | null>
  status: Ref<TStatus | null>
  loading: Ref<boolean>
  error: Ref<string | null>
  isCompleted: ComputedRef<boolean>
  isFailed: ComputedRef<boolean>
  submit: () => Promise<void>
  reset: () => void
}

/**
 * Generic composable for submit -> poll -> complete async tasks.
 *
 * `loading` is true from submit() until the task reaches completed/failed.
 * There is NO gap in between -- the bug that plagued the old code is
 * structurally impossible here.
 *
 * Race-condition protection: if submit() is called again while a previous
 * task is still polling, the old poll results are silently discarded.
 */
export function useAsyncTask<TStatus extends { status: string; error?: string | null }>(
  submitFn: () => Promise<{ task_id: string }>,
  pollFn: (id: string) => Promise<TStatus>,
  options?: AsyncTaskOptions<TStatus>,
): AsyncTaskReturn<TStatus> {
  const taskId = ref<string | null>(null) as Ref<string | null>
  const status = ref<TStatus | null>(null) as Ref<TStatus | null>
  const loading = ref(false)
  const error = ref<string | null>(null) as Ref<string | null>

  let timer: ReturnType<typeof setInterval> | null = null
  let currentId: string | null = null

  const interval = options?.pollInterval ?? 500

  async function submit() {
    stop()
    currentId = null
    loading.value = true
    error.value = null
    taskId.value = null
    status.value = null

    try {
      const resp = await submitFn()
      currentId = resp.task_id
      taskId.value = resp.task_id
      timer = setInterval(poll, interval)
    } catch (e: unknown) {
      currentId = null
      loading.value = false
      error.value = e instanceof Error ? e.message : 'Submit failed'
    }
  }

  async function poll() {
    const id = currentId
    if (!id) return

    try {
      const s = await pollFn(id)
      if (currentId !== id) return
      status.value = s

      if (s.status === 'completed') {
        stop()
        loading.value = false
        options?.onCompleted?.(s)
      } else if (s.status === 'failed') {
        stop()
        loading.value = false
        error.value = s.error || 'Task failed'
        options?.onFailed?.(s)
      }
    } catch {
      if (currentId !== id) return
    }
  }

  function stop() {
    if (timer !== null) {
      clearInterval(timer)
      timer = null
    }
  }

  function reset() {
    stop()
    currentId = null
    taskId.value = null
    status.value = null
    loading.value = false
    error.value = null
  }

  onUnmounted(stop)

  const isCompleted = computed(() => status.value?.status === 'completed')
  const isFailed = computed(() => status.value?.status === 'failed')

  return { taskId, status, loading, error, isCompleted, isFailed, submit, reset }
}
