import PropTypes from 'prop-types';
import React, {useMemo} from 'react';
import {connect} from 'react-redux';
import VM from '@scratch/scratch-vm';

import {setHoveredSprite} from '../reducers/hovered-target';
import {updateAssetDrag} from '../reducers/asset-drag';
import {GUIStoragePropType} from '../gui-config';

// Absence must not be confused with valid false/zero/null state values.
const MISSING_SPRITE_SELECTOR_STORE = Object.freeze({});
const SpriteSelectorStoreContext = React.createContext(MISSING_SPRITE_SELECTOR_STORE);

const SpriteSelectorStoreProvider = ({
    children,
    storage,
    vm,
    dragging,
    hoveredSprite,
    receivedBlocks,
    dispatchSetHoveredSprite,
    onDrag
}) => {
    const value = useMemo(() => ({
        storage,
        vm,
        dragging,
        hoveredSprite,
        receivedBlocks,
        dispatchSetHoveredSprite,
        onDrag
    }), [storage, vm, dragging, hoveredSprite, receivedBlocks, dispatchSetHoveredSprite, onDrag]);
    return (
        <SpriteSelectorStoreContext.Provider value={value}>
            {children}
        </SpriteSelectorStoreContext.Provider>
    );
};
SpriteSelectorStoreProvider.propTypes = {
    children: PropTypes.node,
    storage: GUIStoragePropType,
    vm: PropTypes.instanceOf(VM),
    dragging: PropTypes.bool,
    hoveredSprite: PropTypes.oneOfType([PropTypes.string, PropTypes.number]),
    // Preserve the original && result without coercing it to Boolean.
    receivedBlocks: PropTypes.any,
    dispatchSetHoveredSprite: PropTypes.func.isRequired,
    onDrag: PropTypes.func.isRequired
};

const mapStateToProps = state => ({
    storage: state.scratchGui.config.storage,
    vm: state.scratchGui.vm,
    dragging: state.scratchGui.assetDrag.dragging,
    hoveredSprite: state.scratchGui.hoveredTarget.sprite,
    receivedBlocks: state.scratchGui.hoveredTarget.receivedBlocks
});
const mapDispatchToProps = dispatch => ({
    dispatchSetHoveredSprite: spriteId => {
        dispatch(setHoveredSprite(spriteId));
    },
    onDrag: data => dispatch(updateAssetDrag(data))
});

export {SpriteSelectorStoreContext, MISSING_SPRITE_SELECTOR_STORE};
export default connect(mapStateToProps, mapDispatchToProps)(SpriteSelectorStoreProvider);
