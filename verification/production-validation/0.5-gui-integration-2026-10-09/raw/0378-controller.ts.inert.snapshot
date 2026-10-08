import type { SouiGui, SouiGuiRegistration } from '@soui/native-sdk';

/** Explicit application semantics; no DOM values or component props are projected. */
export interface SouiGuiBindingOptions {
  id: string;
  role: string;
  name: string;
  actions: SouiGuiRegistration['actions'];
  state?: SouiGuiRegistration['state'];
  expose?: boolean;
}

interface Generation {
  gui: SouiGui;
  element: Element;
  id: string;
  role: string;
  name: string;
  names: string[];
  actions: SouiGuiRegistration['actions'];
  state: SouiGuiRegistration['state'];
  unregister: () => void;
}

/** Private, framework-independent controller; commit only after DOM insertion. */
export class GuiBinding {
  private current: Generation | undefined;

  public release(): void {
    const previous = this.current;
    this.current = undefined;
    if (previous) previous.unregister();
  }

  public commit(gui: SouiGui, element: Element | null, options: SouiGuiBindingOptions): void {
    if (!element || options.expose === false) { this.release(); return; }
    const actions: SouiGuiRegistration['actions'] = Object.create(null);
    let names: string[];
    let state: SouiGuiRegistration['state'];
    try {
      if (options.expose !== undefined && typeof options.expose !== 'boolean') throw new TypeError('GUI binding expose must be boolean.');
      if (!options.actions || typeof options.actions !== 'object' || Array.isArray(options.actions))
        throw new TypeError('GUI binding actions must be an explicit map.');
      names = Object.keys(options.actions).sort();
      if (names.length < 1 || names.length > 32) throw new TypeError('GUI binding requires 1–32 named actions.');
      for (const name of names) {
        const action = options.actions[name];
        if (typeof action !== 'function') throw new TypeError('GUI binding actions must be functions.');
        actions[name] = action;
      }
      state = options.state;
      if (state !== undefined && typeof state !== 'function') throw new TypeError('GUI binding state must be a function.');
    } catch (error) {
      // Framework error handlers may retain the component after an invalid update.
      this.release();
      throw error;
    }
    const id = options.id, role = options.role, name = options.name;
    const previous = this.current;
    if (previous && previous.gui === gui && previous.element === element && previous.id === id &&
        previous.role === role && previous.name === name && previous.names.length === names.length &&
        previous.names.every((key, index) => key === names[index])) {
      previous.actions = actions;
      previous.state = state;
      return;
    }
    this.release();
    // Each registration closes over its own generation, never over a reused id.
    const entry: Generation = { gui, element, id, role, name, names, actions, state, unregister: () => {} };
    const handlers: SouiGuiRegistration['actions'] = Object.create(null);
    for (const action of names) handlers[action] = input => entry.actions[action](input);
    entry.unregister = gui.register({ id, role, name, element, actions: handlers,
      state: () => entry.state ? entry.state() : {} });
    this.current = entry;
  }
}
