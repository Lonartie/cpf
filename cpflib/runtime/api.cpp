#include "api.h"

namespace cpf {
   namespace detail {
      void add_damage(node& target, node_damage damage) { target.add_damage(std::move(damage)); }
   }

   node::~node() = default;

   auto node::is_damaged() const -> bool { return !m_damage.empty(); }

   auto node::damage() const -> const std::vector<node_damage>& { return m_damage; }

   void node::add_damage(node_damage damage) { m_damage.push_back(std::move(damage)); }

   void node::copy_damage_to(node& other) const { other.m_damage = m_damage; }
} // namespace cpf

