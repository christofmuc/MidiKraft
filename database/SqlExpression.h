/*
   Copyright (c) 2025 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <string>
#include <vector>

namespace midikraft {

	namespace sqlexpr {

		enum class SqlExprType {
			Atom,
			And,
			Or
		};

		struct SqlExpr {
			/*
			   Lightweight SQL expression tree for building WHERE clauses without
			   ad-hoc string concatenation.
			*/

			SqlExprType type;
			std::string atom;
			std::vector<SqlExpr> children;

			static SqlExpr atomExpr(std::string text)
			{
				SqlExpr e;
				e.type = SqlExprType::Atom;
				e.atom = std::move(text);
				return e;
			}

			static SqlExpr andExpr(std::vector<SqlExpr> nodes)
			{
				SqlExpr e;
				e.type = SqlExprType::And;
				e.children = std::move(nodes);
				return e;
			}

			static SqlExpr orExpr(std::vector<SqlExpr> nodes)
			{
				SqlExpr e;
				e.type = SqlExprType::Or;
				e.children = std::move(nodes);
				return e;
			}
		};

		// Render the expression tree into an SQL fragment, including parentheses
		// where necessary to preserve the intended logical structure.
		std::string toSql(SqlExpr const& expr);

	} // namespace sqlexpr

} // namespace midikraft
