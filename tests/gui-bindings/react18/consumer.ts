import { createSouiGui, type SouiJson } from '@soui/native-sdk';
import { useSouiGuiNode } from '@soui/gui-react';

const gui = createSouiGui();
function Consumer(): void {
  const ref = useSouiGuiNode<HTMLButtonElement>(gui, { id: 'order:42:approve', role: 'button', name: 'Approve',
    actions: { activate: (input: SouiJson) => input }, state: () => ({ state: { ready: true } }) });
  const callback: (node: HTMLButtonElement | null) => void = ref;
  void callback;
  // @ts-expect-error a DOM button ref cannot be used as a div ref
  const wrong: (node: HTMLDivElement | null) => void = ref;
  void wrong;
}
void Consumer;
