import { createSouiGui, type SouiJson } from '@soui/native-sdk';
import { useSouiGuiNode as useReactNode } from '@soui/gui-react';
import { useSouiGuiNode as useVueNode } from '@soui/gui-vue';

const gui = createSouiGui();
const actions = { activate: (input: SouiJson) => input };
function ReactConsumer(): void {
  const ref = useReactNode<HTMLButtonElement>(gui, { id: 'order:42:approve', role: 'button', name: 'Approve', actions });
  const callback: (node: HTMLButtonElement | null) => void = ref;
  void callback;
  // @ts-expect-error actions must be explicitly named functions
  useReactNode(gui, { id: 'bad', role: 'button', name: 'Bad', actions: { activate: 'click' } });
}
function VueConsumer(): void {
  const ref = useVueNode<HTMLButtonElement>(gui, () => ({ id: 'order:42:approve', role: 'button', name: 'Approve', actions }));
  const callback: (node: HTMLButtonElement | null) => void = ref;
  void callback;
  // @ts-expect-error Vue receives a getter, not a one-time options object
  useVueNode(gui, { id: 'bad', role: 'button', name: 'Bad', actions });
}
void ReactConsumer;
void VueConsumer;
