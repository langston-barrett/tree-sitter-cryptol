// Print the paths (read from the file given as the first argument, one per
// line, relative to the second argument) that tree-sitter fails to parse
// without errors.
//
// This uses `hasError`, which also detects tokens that error recovery
// inserted, even hidden ones (e.g., layout separators); `tree-sitter parse`
// does not report those.
import Parser from 'tree-sitter';
import fs from 'node:fs';
import path from 'node:path';
import Language from '../bindings/node/index.js';

const [listFile, root] = process.argv.slice(2);
const parser = new Parser();
parser.setLanguage(Language);
for (const file of fs.readFileSync(listFile, 'utf8').split('\n')) {
  if (file === '') continue;
  const source = fs.readFileSync(path.join(root, file), 'utf8');
  // Use a callback, so that large files are not limited by the binding's
  // default buffer size.
  const tree = parser.parse((index) => source.slice(index, index + 65536));
  if (tree.rootNode.hasError) {
    console.log(file);
  }
}
