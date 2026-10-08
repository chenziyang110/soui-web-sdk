import { onActivated, onBeforeUnmount, onDeactivated, onMounted, onUpdated, watchPostEffect } from 'vue';
import type { SouiGui } from '@soui/native-sdk';
import { GuiBinding } from '../../shared/controller.js';
import type { SouiGuiBindingOptions } from '../../shared/controller.js';

export type { SouiGuiBindingOptions } from '../../shared/controller.js';

/** Call during setup; provide a getter so post-render updates can read current options. */
export function useSouiGuiNode<E extends Element = HTMLElement>(
  gui: SouiGui, options: () => SouiGuiBindingOptions
): (element: E | null) => void {
  const binding = new GuiBinding();
  let element: E | null = null;
  let mounted = false;
  let active = true;
  const commit = () => {
    try {
      const value = options();
      if (mounted && active) binding.commit(gui, element, value);
    } catch (error) {
      binding.release();
      throw error;
    }
  };
  watchPostEffect(commit);
  onMounted(() => { mounted = true; commit(); });
  onUpdated(commit);
  onActivated(() => { active = true; commit(); });
  onDeactivated(() => { active = false; binding.release(); });
  onBeforeUnmount(() => { mounted = false; active = false; binding.release(); });
  return next => {
    if (element !== next) binding.release();
    element = next;
  };
}
