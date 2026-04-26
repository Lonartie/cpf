#include "api.h"

#include <iomanip>
#include <ostream>

namespace cpf {
   namespace {
      void write_indent(std::ostream& os, std::size_t indent) {
         for (std::size_t i = 0; i < indent; ++i) {
            os << "  ";
         }
      }

      void write_range(std::ostream& os, const source_range& range) {
         os << range.begin.offset << ".." << range.end.offset << " (" << range.begin.line << ':'
            << range.begin.column << '-' << range.end.line << ':' << range.end.column << ')';
      }
   } // namespace

   namespace detail {
      void add_damage(node& target, node_damage damage) { target.add_damage(std::move(damage)); }
   }

   node::~node() = default;

   auto node::is_damaged() const -> bool { return !m_damage.empty(); }

   auto node::damage() const -> const std::vector<node_damage>& { return m_damage; }

   void node::add_damage(node_damage damage) { m_damage.push_back(std::move(damage)); }

   void node::copy_damage_to(node& other) const { other.m_damage = m_damage; }

   std::ostream& operator<<(std::ostream& os, const token_sequence& sequence) {
      os << "token_sequence(\n";
      write_indent(os, 1);
      os << "input = " << std::quoted(sequence.input) << ",\n";
      write_indent(os, 1);
      os << "tokens = [";
      if (!sequence.tokens.empty()) {
         os << '\n';
         for (std::size_t index = 0; index < sequence.tokens.size(); ++index) {
            const auto& token = sequence.tokens[index];
            write_indent(os, 2);
            os << '[' << index << "] { ";
            if (token.invalid) {
               os << "invalid = true, ";
            } else {
               os << "symbol = " << token.symbol << ", ";
            }
            os << "text = " << std::quoted(token.text.text) << ", range = ";
            write_range(os, token.text.range);
            os << " }";
            if (index + 1 != sequence.tokens.size()) {
               os << ',';
            }
            os << '\n';
         }
         write_indent(os, 1);
      }
      os << "]\n)";
      return os;
   }
} // namespace cpf

