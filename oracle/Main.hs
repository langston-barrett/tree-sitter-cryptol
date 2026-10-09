-- | Classify Cryptol source files using Cryptol's own lexer and parser.
--
-- For each file argument, prints one line:
--
-- > ok       FILE
-- > syntax   FILE<TAB>MESSAGE   (lexical or grammatical error)
-- > semantic FILE<TAB>MESSAGE   (rejected by a check in the parser's actions)
--
-- Only files labelled @syntax@ are expected to be rejected by tree-sitter.
module Main (main) where

import Control.Exception (SomeException, displayException, try)
import qualified Data.ByteString as BS
import qualified Data.Text as T
import qualified Data.Text.Encoding as T
import System.Environment (getArgs)

import Cryptol.Parser
  (Config(..), ParseError(..), defaultConfig, guessPreProc, parseModule, ppError)
import Cryptol.Parser.Lexer (Token(..), TokenT(..), Located(..))
import qualified Cryptol.Parser.Lexer as Lexer

main :: IO ()
main = mapM_ classify =<< getArgs

classify :: FilePath -> IO ()
classify path =
  do bytes <- try (BS.readFile path)
     case bytes of
       Left e -> report "error   " (displayException (e :: SomeException))
       Right bs ->
         case T.decodeUtf8' bs of
           Left _ -> report "syntax  " "invalid UTF-8"
           Right src -> classifyText path src
  where
  report label msg = putStrLn (label ++ " " ++ path ++ "\t" ++ msg)

classifyText :: FilePath -> T.Text -> IO ()
classifyText path src =
  do let cfg = defaultConfig { cfgSource = path, cfgPreProc = guessPreProc path }
         lexErr = [ t | Located _ t@(Token (Err _) _) <- fst (Lexer.lexer cfg src) ]
     case parseModule cfg src of
       Right _ -> putStrLn ("ok       " ++ path)
       Left err ->
         let label = case err of
                       HappyErrorMsg {} | null lexErr -> "semantic"
                       _ -> "syntax  "
         in putStrLn (label ++ " " ++ path ++ "\t" ++ oneLine (show (ppError err)))
  where
  oneLine = unwords . words
