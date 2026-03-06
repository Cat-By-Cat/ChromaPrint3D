import { fetchColorDBs, fetchDefaults } from '../api/convert'

export async function fetchParamPanelBootstrap() {
  const [defaults, colorDBs] = await Promise.all([fetchDefaults(), fetchColorDBs()])
  return { defaults, colorDBs }
}

export async function fetchAvailableColorDBs() {
  return fetchColorDBs()
}
