#pragma once

#include "model/grammar.h"

#include <string_view>

namespace cpf {
   /// @brief Parses CPF grammar text into an in-memory grammar model.
   /// @param text Grammar source text.
   /// @return Parsed grammar representation.
   grammar parse_grammar(std::string_view text);
} // namespace cpf
