export async function loadLocalConfiguration() {
  const { describe } = await import('./details.mjs');
  const response = await fetch(new URL('./config.json', import.meta.url));
  if (!response.ok) throw new Error(`Configuration request failed (${response.status}).`);
  return describe(await response.json());
}
