#include "api.h"

#include <algorithm>
#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <unordered_set>

namespace cpf {
   namespace {
      [[nodiscard]] auto line_offsets_of(std::string_view text) -> std::vector<std::size_t> {
         auto offsets = std::vector<std::size_t>{0};
         for (std::size_t index = 0; index < text.size(); ++index) {
            if (text[index] == '\n') {
               offsets.push_back(index + 1);
            }
         }
         return offsets;
      }

      [[nodiscard]] auto position_from_offsets(const std::vector<std::size_t>& offsets, std::size_t offset,
                                               source_position base) -> source_position {
         if (offsets.empty()) {
            return base;
         }

         const auto line = static_cast<std::size_t>(
               std::upper_bound(offsets.begin(), offsets.end(), offset) - offsets.begin() - 1);
         const auto line_offset = offsets[line];
         base.offset += offset;
         base.line += line;
         if (line == 0) {
            base.column += offset - line_offset;
         } else {
            base.column = 1 + offset - line_offset;
         }
         return base;
      }

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

   void source_mapper::append_source(std::string_view source, std::size_t id) {
      if (contains(id)) {
         throw std::runtime_error{"Source id '" + std::to_string(id) + "' was registered more than once"};
      }
      m_sources.emplace(id, source_node{std::string{source}, line_offsets_of(source)});
   }

   void source_mapper::append(std::size_t from_id, std::size_t to_id, source_range replacement_range) {
      if (!contains(from_id)) {
         throw std::runtime_error{"Unknown source id '" + std::to_string(from_id) + "' used as replacement source"};
      }
      if (!contains(to_id)) {
         throw std::runtime_error{"Unknown source id '" + std::to_string(to_id) + "' used as replacement target"};
      }

      const auto& from = m_sources.at(from_id);
      const auto& to = m_sources.at(to_id);
      if (replacement_range.begin.offset > replacement_range.end.offset ||
          replacement_range.end.offset > to.text.size()) {
         throw std::runtime_error{"Replacement range is outside the target source '" + std::to_string(to_id) + "'"};
      }
      if (replacement_range.end.offset - replacement_range.begin.offset != from.text.size()) {
         throw std::runtime_error{"Replacement range size does not match replacement source '" + std::to_string(from_id) +
                                  "'"};
      }

      auto& incoming = m_incoming[to_id];
      for (const auto& edge: incoming) {
         const auto overlaps = edge.replacement_range.begin.offset < replacement_range.end.offset &&
                               replacement_range.begin.offset < edge.replacement_range.end.offset;
         if (overlaps) {
            throw std::runtime_error{"Replacement ranges overlap inside target source '" + std::to_string(to_id) + "'"};
         }
      }

      auto edge = replacement_edge{from_id, to_id, replacement_range};
      incoming.push_back(edge);
      std::ranges::sort(incoming, [](const auto& lhs, const auto& rhs) {
         return lhs.replacement_range.begin.offset < rhs.replacement_range.begin.offset;
      });
      m_outgoing[from_id].push_back(edge);
   }

   auto source_mapper::contains(std::size_t id) const -> bool { return m_sources.contains(id); }

   auto source_mapper::source(std::size_t id) const -> std::string_view {
      if (auto found = m_sources.find(id); found != m_sources.end()) {
         return found->second.text;
      }
      throw std::runtime_error{"Unknown source id '" + std::to_string(id) + "'"};
   }

   auto source_mapper::location_for(std::size_t id, std::size_t offset) const -> std::optional<source_location> {
      auto found = m_sources.find(id);
      if (found == m_sources.end() || offset > found->second.text.size()) {
         return std::nullopt;
      }
      return position_from_offsets(found->second.line_offsets, offset, source_position{});
   }

   auto source_mapper::offset_for(std::size_t id, source_location location) const -> std::optional<std::size_t> {
      auto found = m_sources.find(id);
      if (found == m_sources.end()) {
         return std::nullopt;
      }

      if (location.line != 0 && location.column != 0) {
         if (location.line > found->second.line_offsets.size()) {
            return std::nullopt;
         }
         const auto line_index = location.line - 1;
         const auto line_begin = found->second.line_offsets[line_index];
         const auto offset = line_begin + location.column - 1;
         const auto line_end = line_index + 1 < found->second.line_offsets.size()
                                     ? found->second.line_offsets[line_index + 1]
                                     : found->second.text.size();
         if (offset > found->second.text.size() || offset >= line_end) {
            if (!(line_index + 1 == found->second.line_offsets.size() && offset == found->second.text.size())) {
               return std::nullopt;
            }
         }
         return offset;
      }

      if (location.offset <= found->second.text.size()) {
         return location.offset;
      }
      return std::nullopt;
   }

   auto source_mapper::resolve_once(source_location location, std::size_t id) const
         -> std::optional<resolved_source_location> {
      auto offset = offset_for(id, location);
      if (!offset.has_value()) {
         return std::nullopt;
      }

      const auto canonical = location_for(id, *offset);
      if (!canonical.has_value()) {
         return std::nullopt;
      }

      if (auto incoming = m_incoming.find(id); incoming != m_incoming.end()) {
         for (const auto& edge: incoming->second) {
            if (edge.replacement_range.begin.offset <= *offset && *offset < edge.replacement_range.end.offset) {
               auto mapped = location_for(edge.from_id, *offset - edge.replacement_range.begin.offset);
               if (!mapped.has_value()) {
                  return std::nullopt;
               }
               return resolved_source_location{*mapped, edge.from_id};
            }
         }
      }

      return resolved_source_location{*canonical, id};
   }

   auto source_mapper::resolve(source_location location, std::size_t id) const -> std::optional<resolved_source_location> {
      auto current = resolve_once(location, id);
      if (!current.has_value()) {
         return std::nullopt;
      }

      auto visited = std::unordered_set<std::string>{};
      while (true) {
         const auto key = std::to_string(current->id) + ":" + std::to_string(current->location.offset);
         if (!visited.insert(key).second) {
            return current;
         }

         auto next = resolve_once(current->location, current->id);
         if (!next.has_value()) {
            return std::nullopt;
         }
         if (next->id == current->id && next->location.offset == current->location.offset) {
            return next;
         }
         current = next;
      }
   }

   auto source_mapper::resolve_from(source_location location, std::size_t id) const
         -> std::vector<resolved_source_location> {
      auto offset = offset_for(id, location);
      if (!offset.has_value()) {
         return {};
      }

      auto results = std::vector<resolved_source_location>{};
      auto visited = std::unordered_set<std::string>{};

      std::function<void(std::size_t, std::size_t)> visit = [&](std::size_t current_id, std::size_t current_offset) {
         const auto key = std::to_string(current_id) + ":" + std::to_string(current_offset);
         if (!visited.insert(key).second) {
            return;
         }

         auto emitted = false;
         if (auto outgoing = m_outgoing.find(current_id); outgoing != m_outgoing.end()) {
            for (const auto& edge: outgoing->second) {
               const auto target_offset = edge.replacement_range.begin.offset + current_offset;
               auto mapped = location_for(edge.to_id, target_offset);
               if (!mapped.has_value()) {
                  continue;
               }
               emitted = true;
               visit(edge.to_id, target_offset);
            }
         }

         if (!emitted) {
            if (auto canonical = location_for(current_id, current_offset); canonical.has_value()) {
               results.push_back(resolved_source_location{*canonical, current_id});
            }
         }
      };

      visit(id, *offset);
      std::ranges::sort(results, [](const auto& lhs, const auto& rhs) {
         if (lhs.id != rhs.id) {
            return lhs.id < rhs.id;
         }
         return lhs.location.offset < rhs.location.offset;
      });
      return results;
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

