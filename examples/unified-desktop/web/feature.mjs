import { label } from './module-label.mjs';

export async function loadConfiguration() {
  const response = await fetch(new URL('./config.json', import.meta.url));
  if (!response.ok) throw new Error(`Asset request failed: ${response.status}`);
  return { label, config: await response.json() };
}
