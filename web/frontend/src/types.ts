// ---- Input type ----

export type InputType = 'raster' | 'vector'

export interface ImageDimensions {
  width: number
  height: number
}

// ---- Convert parameters (matches backend ConvertRasterRequest) ----

export interface ConvertRasterParams {
  db_names?: string[]
  print_mode?: string // "0.08x5" | "0.04x10"
  color_space?: string // "lab" | "rgb"
  max_width?: number
  max_height?: number
  target_width_mm?: number
  target_height_mm?: number
  scale?: number
  k_candidates?: number
  cluster_count?: number
  dither?: string // "none" | "blue_noise" | "floyd_steinberg"
  dither_strength?: number // 0.0 ~ 1.0
  allowed_channels?: PaletteChannel[]
  model_enable?: boolean
  model_only?: boolean
  model_threshold?: number
  model_margin?: number
  flip_y?: boolean
  pixel_mm?: number
  layer_height_mm?: number
  generate_preview?: boolean
  generate_source_mask?: boolean
}

// ---- Vector convert parameters (matches backend ConvertVectorRequest) ----

export interface ConvertVectorParams {
  db_names?: string[]
  print_mode?: string
  color_space?: string
  target_width_mm?: number
  target_height_mm?: number
  k_candidates?: number
  model_enable?: boolean
  model_only?: boolean
  model_threshold?: number
  model_margin?: number
  flip_y?: boolean
  layer_height_mm?: number
  allowed_channels?: PaletteChannel[]
  generate_preview?: boolean
  tessellation_tolerance_mm?: number
  gradient_dither?: string
  gradient_dither_strength?: number
}

// Superset type for the ParamPanel v-model (all optional fields from both)
export type ConvertAnyParams = ConvertRasterParams & ConvertVectorParams

// ---- Match statistics ----

export interface MatchStats {
  clusters_total: number
  db_only: number
  model_fallback: number
  model_queries: number
  avg_db_de: number
  avg_model_de: number
}

export type LayerOrderSummary = 'Top2Bottom' | 'Bottom2Top'

export interface LayerPreviewChannel {
  channel_idx: number
  color: string
  material: string
}

export interface LayerPreviewsSummary {
  enabled: boolean
  layers: number
  width: number
  height: number
  layer_order: LayerOrderSummary
  palette: LayerPreviewChannel[]
  artifacts: string[]
}

// ---- Task result (populated when status === 'completed') ----

export interface TaskResult {
  input_width: number
  input_height: number
  resolved_pixel_mm: number
  physical_width_mm: number
  physical_height_mm: number
  stats: MatchStats
  has_3mf: boolean
  has_preview: boolean
  has_source_mask: boolean
  layer_previews?: LayerPreviewsSummary
}

// ---- Task status (matches backend TaskInfoToJson) ----

export type TaskStatusValue = 'pending' | 'running' | 'completed' | 'failed'

export type ConvertStage =
  | 'loading_resources'
  | 'preprocessing'
  | 'matching'
  | 'building_model'
  | 'exporting'
  | 'unknown'

export interface TaskStatus {
  id: string
  status: TaskStatusValue
  stage: ConvertStage
  progress: number
  created_at_ms: number
  error: string | null
  result: TaskResult | null
}

// ---- ColorDB info (matches backend ColorDBInfoToJson) ----

export interface PaletteChannel {
  color: string
  material: string
}

export interface ColorDBInfo {
  name: string
  num_channels: number
  num_entries: number
  max_color_layers: number
  base_layers: number
  layer_height_mm: number
  line_width_mm: number
  palette: PaletteChannel[]
  source?: 'global' | 'session'
  material_type?: string
  vendor?: string
}

// ---- Calibration ----

export interface GenerateBoardRequest {
  palette: PaletteChannel[]
  color_layers?: number
  layer_height_mm?: number
}

export interface GenerateBoardResponse {
  board_id: string
  meta: Record<string, unknown>
}

export interface Generate8ColorBoardRequest {
  palette: PaletteChannel[]
  board_index: number // 1 or 2
}

// ---- Health response ----

export interface HealthResponse {
  status: string
  version: string
  active_tasks: number
  total_tasks: number
}

// ---- Matting ----

export interface MattingMethodInfo {
  key: string
  name: string
  description: string
}

export interface MattingTimingInfo {
  preprocess_ms: number
  inference_ms: number
  postprocess_ms: number
  decode_ms: number
  encode_ms: number
  provider_ms: number
  pipeline_ms: number
}

export type MattingTaskStatusValue = 'pending' | 'running' | 'completed' | 'failed'

export interface MattingTaskStatus {
  id: string
  status: MattingTaskStatusValue
  method: string
  error: string | null
  width: number
  height: number
  timing: MattingTimingInfo | null
}

// ---- Vectorize ----

export interface VectorizeParams {
  num_colors?: number
  min_region_area?: number
  min_contour_area?: number
  min_hole_area?: number
  contour_simplify?: number
  topology_cleanup?: number
  enable_coverage_fix?: boolean
  min_coverage_ratio?: number
  svg_enable_stroke?: boolean
  svg_stroke_width?: number
}

export interface VectorizeTimingInfo {
  decode_ms: number
  vectorize_ms: number
  pipeline_ms: number
}

export type VectorizeTaskStatusValue = 'pending' | 'running' | 'completed' | 'failed'

export interface VectorizeTaskStatus {
  id: string
  status: VectorizeTaskStatusValue
  error: string | null
  width: number
  height: number
  num_shapes: number
  svg_size: number
  timing: VectorizeTimingInfo | null
}

// ---- Default config response ----

export interface DefaultConfig {
  scale: number
  max_width: number
  max_height: number
  target_width_mm: number
  target_height_mm: number
  print_mode: string
  color_space: string
  k_candidates: number
  cluster_count: number
  dither: string
  dither_strength: number
  model_enable: boolean
  model_only: boolean
  model_threshold: number
  model_margin: number
  flip_y: boolean
  pixel_mm: number
  layer_height_mm: number
  generate_preview: boolean
  generate_source_mask: boolean
}

// ---- Bed size presets ----

export interface BedPreset {
  label: string
  width: number
  height: number
}

export const BED_PRESETS: BedPreset[] = [
  { label: 'Bambu Lab A1 mini', width: 180, height: 180 },
  { label: 'Bambu Lab A1 / P1 / X1', width: 256, height: 256 },
  { label: 'Prusa MK4', width: 250, height: 210 },
  { label: 'Ender 3', width: 220, height: 220 },
  { label: '自定义', width: 0, height: 0 },
]

// ---- Pixel size presets ----

export interface PixelSizePreset {
  label: string
  value: number
}

export const PIXEL_SIZE_PRESETS: PixelSizePreset[] = [
  { label: '0.22 mm (0.2mm 喷嘴)', value: 0.22 },
  { label: '0.42 mm (0.4mm 喷嘴)', value: 0.42 },
  { label: '自定义', value: 0 },
]
