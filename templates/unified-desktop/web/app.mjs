import { loadLocalConfiguration } from './feature.mjs';
import { invoke } from './soui/desktop.mjs';

const status = document.getElementById('status');
status.textContent = 'Frontend module loaded. Choose a check above.';
function bind(id, action) {
  const button = document.getElementById(id);
  button.addEventListener('click', async () => {
    button.disabled = true;
    try { status.textContent = JSON.stringify(await action(), null, 2); }
    catch (error) { status.textContent = `Request failed: ${error.message}`; }
    finally { button.disabled = false; }
  });
}
bind('info', () => invoke('app.info', null));
bind('assets', loadLocalConfiguration);
