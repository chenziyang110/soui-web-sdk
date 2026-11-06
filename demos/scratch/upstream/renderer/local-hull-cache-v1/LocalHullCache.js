class LocalHullCache {
    constructor ({maxEntries = 1024, maxPoints = 131072} = {}) {
        this._maxEntries = maxEntries;
        this._maxPoints = maxPoints;
        this._entries = new Map();
        this._pointCount = 0;
    }

    get size () {
        return this._entries.size;
    }

    get pointCount () {
        return this._pointCount;
    }

    get (skin, revision, width, height) {
        const entry = this._entries.get(skin);
        if (!entry) return;
        if (entry.revision !== revision || entry.width !== width || entry.height !== height) {
            this.delete(skin);
            return;
        }
        this._entries.delete(skin);
        this._entries.set(skin, entry);
        return entry.points;
    }

    put (skin, revision, width, height, points) {
        this.delete(skin);
        if (this._maxEntries < 1 || points.length > this._maxPoints) return points;
        while (this._entries.size >= this._maxEntries || this._pointCount + points.length > this._maxPoints) {
            this.delete(this._entries.keys().next().value);
        }
        for (const point of points) Object.freeze(point);
        Object.freeze(points);
        this._entries.set(skin, {revision, width, height, points});
        this._pointCount += points.length;
        return points;
    }

    delete (skin) {
        const entry = this._entries.get(skin);
        if (!entry) return;
        this._pointCount -= entry.points.length;
        this._entries.delete(skin);
    }

    clear () {
        this._entries.clear();
        this._pointCount = 0;
    }
}

module.exports = LocalHullCache;
