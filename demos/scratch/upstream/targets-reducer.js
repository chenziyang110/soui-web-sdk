// SPDX-License-Identifier: AGPL-3.0-only
// Derived from Scratch GUI targets reducer at the pinned upstream release.
const UPDATE_TARGET_LIST = 'scratch-gui/targets/UPDATE_TARGET_LIST';
const HIGHLIGHT_TARGET = 'scratch-gui/targets/HIGHLIGHT_TARGET';
const initialState = {sprites: {}, stage: {}, highlightedTargetId: null, highlightedTargetTime: null};

// Preserve one new record per update: VM-owned nested objects can mutate in
// place, so shallow reuse would hide changes from Redux consumers. Traverse
// once and avoid the two filters and temporary Object.assign source per sprite.
const reducer = function (state, action) {
    if (typeof state === 'undefined') state = initialState;
    switch (action.type) {
    case UPDATE_TARGET_LIST: {
        const sprites = {};
        let stage;
        let order = 0;
        for (const target of action.targets) {
            if (target.isStage) {
                if (typeof stage === 'undefined') stage = target;
            } else {
                // Match upstream spread precedence, including an own `order`.
                const entry = {order: order++, ...target};
                if (target.id === '__proto__') Object.assign(sprites, {[target.id]: entry});
                else sprites[target.id] = entry;
            }
        }
        return Object.assign({}, state, {sprites, stage: stage || {}, editingTarget: action.editingTarget});
    }
    case HIGHLIGHT_TARGET:
        return Object.assign({}, state, {
            highlightedTargetId: action.targetId, highlightedTargetTime: action.updateTime
        });
    default:
        return state;
    }
};
const updateTargets = (targetList, editingTarget) => ({type: UPDATE_TARGET_LIST, targets: targetList, editingTarget});
const highlightTarget = targetId => ({type: HIGHLIGHT_TARGET, targetId, updateTime: Date.now()});
export {reducer as default, initialState as targetsInitialState, updateTargets, highlightTarget};
