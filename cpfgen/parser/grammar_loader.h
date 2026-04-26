#pragma once

#include <cpflib>

#include "model/grammar.h"

#include <filesystem>
#include <unordered_map>
#include <vector>

namespace cpf {
   /// @brief Original file anchor for one leaf source node tracked by the grammar loader.
   struct grammar_source_origin {
      std::filesystem::path path;
      source_position begin;
   };

   /// @brief Result of loading a grammar file together with all preprocessed `@import` dependencies.
   struct loaded_grammar {
      grammar parsed_grammar;
      std::vector<std::filesystem::path> dependencies;
      std::string preprocessed_source;
      source_mapper mapper;
      std::size_t preprocessed_source_id = 0;
      std::unordered_map<std::size_t, grammar_source_origin> source_origins;
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
