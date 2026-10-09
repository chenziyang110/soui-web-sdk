import fs from 'node:fs';
// Both native profiles and all page views receive this exact file.
export function fixtureAsset(sourcePath:string) {
  return {name:'reference-fixed-fixture',generateBundle(this:{emitFile:(asset:unknown)=>void}) {
    this.emitFile({type:'asset',fileName:'reference-fixture.json',
      source:fs.readFileSync(sourcePath)});
  }};
}
