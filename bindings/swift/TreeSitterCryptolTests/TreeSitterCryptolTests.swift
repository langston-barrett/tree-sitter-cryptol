import XCTest
import SwiftTreeSitter
import TreeSitterCryptol

final class TreeSitterCryptolTests: XCTestCase {
    func testCanLoadGrammar() throws {
        let parser = Parser()
        let language = Language(language: tree_sitter_cryptol())
        XCTAssertNoThrow(try parser.setLanguage(language),
                         "Error loading Cryptol grammar")
    }
}
