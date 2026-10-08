import { useCallback, useLayoutEffect, useRef } from 'react';
import type { RefCallback } from 'react';
import type { SouiGui } from '@soui/native-sdk';
import { GuiBinding } from '../../shared/controller.js';
import type { SouiGuiBindingOptions } from '../../shared/controller.js';

export type { SouiGuiBindingOptions } from '../../shared/controller.js';

/** Borrow the application's per-document registry and bind one actual DOM element. */
export function useSouiGuiNode<E extends Element = HTMLElement>(
  gui: SouiGui, options: SouiGuiBindingOptions
): RefCallback<E> {
  const controller = useRef<GuiBinding | null>(null);
  if (controller.current === null) controller.current = new GuiBinding();
  const binding = controller.current;
  const element = useRef<E | null>(null);
  const ref = useCallback((next: E | null): void => {
    if (element.current !== next) binding.release();
    element.current = next;
  }, [binding]);

  // Updating render refs here would expose callbacks from an abandoned render.
  useLayoutEffect(() => { binding.commit(gui, element.current, options); });
  useLayoutEffect(() => () => { binding.release(); }, [binding]);
  return ref;
}
