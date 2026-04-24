#pragma once

#include "model/grammar.h"

#include <string>

namespace cpf {
   /// @brief Generated C++ translation units for a grammar.
   struct generated_code {
      std::string header;
      std::string source;
   };

   /// @brief Generates C++ source code for a parsed grammar.
   /// @param grammar Parsed grammar description.
   /// @param base_name Base file name to embed into generated includes and parser names.
   /// @return Generated header and source contents.
   generated_code generate_code(const grammar& grammar, const std::string& base_name);
} // namespace cpf

