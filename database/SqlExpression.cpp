/*
   Copyright (c) 2025 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/
#include "SqlExpression.h"

namespace midikraft {

	namespace {

		void toSqlInternal(sqlexpr::SqlExpr const& expr, std::string& out)
		{
			using namespace sqlexpr;

			switch (expr.type) {
			case SqlExprType::Atom:
				out += expr.atom;
				break;

			case SqlExprType::And:
			case SqlExprType::Or:
			{
				if (expr.children.empty()) {
					return;
				}

				const char* op = (expr.type == SqlExprType::And) ? " AND " : " OR ";

				if (expr.children.size() == 1) {
					toSqlInternal(expr.children.front(), out);
				}
				else {
					out += "(";
					bool first = true;
					for (auto const& child : expr.children) {
						if (!first) {
							out += op;
						}
						toSqlInternal(child, out);
						first = false;
					}
					out += ")";
				}
				break;
			}
			}
		}

	} // anonymous namespace

	namespace sqlexpr {

		std::string toSql(SqlExpr const& expr)
		{
			std::string result;
			toSqlInternal(expr, result);
			return result;
		}

	} // namespace sqlexpr

} // namespace midikraft

