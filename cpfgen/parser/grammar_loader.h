#pragma once

#include "model/grammar.h"

#include <filesystem>
#include <vector>

namespace cpf {
   /// @brief Result of loading a grammar file together with all preprocessed `@import` dependencies.
   struct loaded_grammar {
      grammar parsed_grammar;
      std::vector<std::filesystem::path> dependencies;
   };

   /// @brief Preprocesses `@import` directives and parses the resulting grammar file.
   /// @param path Root grammar file path.
   /// @return Parsed grammar and all loaded file dependencies.
   loaded_grammar load_grammar_file(const std::filesystem::path& path);

   /// @brief Preprocesses `@import` directives and parses the resulting grammar file.
   /// @param path Root grammar file path.
   /// @return Parsed grammar only.
   grammar parse_grammar_file(const std::filesystem::path& path);
} // namespace cpf
