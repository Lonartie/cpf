#pragma once

#include "model/grammar.h"

#include <filesystem>
#include <vector>

namespace cpf {
   /// @brief Result of loading a grammar file together with all imported files.
   struct loaded_grammar {
      grammar parsed_grammar;
      std::vector<std::filesystem::path> dependencies;
   };

   /// @brief Loads and parses a grammar file including all transitive imports.
   /// @param path Root grammar file path.
   /// @return Parsed grammar and all loaded file dependencies.
   loaded_grammar load_grammar_file(const std::filesystem::path& path);

   /// @brief Loads and parses a grammar file including all transitive imports.
   /// @param path Root grammar file path.
   /// @return Parsed grammar only.
   grammar parse_grammar_file(const std::filesystem::path& path);
} // namespace cpf

