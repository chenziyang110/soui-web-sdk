"""Extract unchanged old production method bodies, never manufacture a baseline formula."""
import hashlib
import json
from pathlib import Path
import sys

source, output = map(Path, sys.argv[1:])
raw = source.read_bytes()
text = raw.decode('utf-8-sig')
signatures = [
    'void WebLayerImpl::setScrollPositionDouble(blink::WebDoublePoint position)',
    'bool WebLayerImpl::setScrollPositionFromCompositor(uint64_t scrollEpoch,',
]
methods = []
for signature in signatures:
    if text.count(signature) != 1:
        raise ValueError('Production method is missing or ambiguous: ' + signature)
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 0
    end = None
    for i in range(brace, len(text)):
        if text[i] == '{': depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                end = i + 1
                break
    if end is None:
        raise ValueError('Unterminated production method')
    methods.append(text[start:end])
generated = '#include "baseline_ack_shim.h"\n#include <cmath>\nnamespace cc {\n' + '\n\n'.join(methods) + '\n}\n'
output.write_text(generated, encoding='utf-8')
receipt = {'schema': 'soui.baseline-ack-extraction/v1',
           'source': str(source.resolve()), 'sourceBytes': len(raw),
           'sourceSha256': hashlib.sha256(raw).hexdigest().upper(),
           'methods': [{'signature': sig, 'extractedUtf8Sha256': hashlib.sha256(body.encode('utf-8')).hexdigest().upper()}
                       for sig, body in zip(signatures, methods)],
           'outputSha256': hashlib.sha256(output.read_bytes()).hexdigest().upper(),
           'bodyRewritten': False,
           'scope': 'Two unchanged production methods with a controlled didScroll canonical callback; no real DOM, GraphicsLayer, or GUI.'}
output.with_suffix('.receipt.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
