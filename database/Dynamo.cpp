#include "Dynamo.h"

#include <boost/format.hpp>

#include "Logger.h"

std::string toString(Aws::String const &aws) {
	return std::string(aws.c_str(), aws.size());
}

int toInteger(Aws::String const &aws) {
	return std::stoi(aws.c_str());
}

bool getStringIfSet(Aws::Map<Aws::String, Aws::DynamoDB::Model::AttributeValue> &dbresult, const char *key, std::string &outString) {
	if (dbresult.find(key) != dbresult.end()) {
		outString = toString(dbresult[key].GetS());
		return true;
	}
	return false;
}

bool getBufferIfSet(Aws::Map<Aws::String, Aws::DynamoDB::Model::AttributeValue> &dbresult, const char *key, std::vector<uint8> &outBuffer) {
	if (dbresult.find(key) != dbresult.end()) {
		auto buffer = dbresult[key].GetB();
		outBuffer = std::vector<uint8>(buffer.GetUnderlyingData(), buffer.GetUnderlyingData() + buffer.GetLength());
		return true;
	}
	return false;
}

bool getNumberIfSet(Aws::Map<Aws::String, Aws::DynamoDB::Model::AttributeValue> &dbresult, const char *key, int &out) {
	if (dbresult.find(key) != dbresult.end()) {
		out = toInteger(dbresult[key].GetN());
		return true;
	}
	return false;
}

bool getBoolIfSet(Aws::Map<Aws::String, Aws::DynamoDB::Model::AttributeValue> &dbresult, const char *key, bool &out) {
	if (dbresult.find(key) != dbresult.end()) {
		out = dbresult[key].GetBool();
		return true;
	}
	return false;
}

bool getStringSetIfSet(Aws::Map<Aws::String, Aws::DynamoDB::Model::AttributeValue> &dbresult, const char *key, std::vector<std::string> &out) {
	if (dbresult.find(key) != dbresult.end()) {
		out.clear();
		auto strings = dbresult[key].GetSS();
		for (auto s : strings) {
			out.push_back(toString(s));
		}
		return true;
	}
	return false;
}

TDynamoValue stringAttribute(std::string const &value)
{
	// Create a string valued attribute
	return Aws::DynamoDB::Model::AttributeValue().SetS(value.c_str());
}

void DynamoDict::addAttribute(std::string const &name, std::string const &value)
{
	emplace(name.c_str(), stringAttribute(value));
}

void DynamoDict::addAttribute(std::string const &name, int const value)
{
	std::string numberAsString = (boost::format("%d") % value).str();
	emplace(name.c_str(), Aws::DynamoDB::Model::AttributeValue().SetN(numberAsString.c_str()));
}

void DynamoDict::addAttribute(std::string const &name, std::vector<uint8> const &value)
{
	Aws::Utils::ByteBuffer buffer(value.data(), value.size());
	emplace(name.c_str(), Aws::DynamoDB::Model::AttributeValue().SetB(buffer));
}

DynamoUpdateItem::DynamoUpdateItem(std::string const &table, std::set<const char *> const &keyNames) :
	keyNames_(keyNames)
{
	SetTableName(table.c_str());
}

void DynamoUpdateItem::addDict(DynamoDict &dict)
{
	// First copy the keys to be used
	for (auto key : keyNames_) {
		AddKey(key, dict[key]);
	}

	// Now setup all non-key attributes
	for (auto item : dict) {
		bool isKey = false;
		for (auto key : keyNames_) {
			if (key == item.first) {
				isKey = true;
			}
		}
		if (!isKey) {
			addUpdateAttribute(toString(item.first), item.second);
		}
	}

	// Calculate the update expression
	setUpdateExpression();
}

void DynamoUpdateItem::addUpdateAttribute(std::string const &name, std::string const &value)
{
	addUpdateAttribute(name, stringAttribute(value));
}

void DynamoUpdateItem::addUpdateAttribute(std::string const &name, TDynamoValue const &value)
{
	std::string attributeName = (boost::format("#%s") % name).str();
	AddExpressionAttributeNames(attributeName.c_str(), name.c_str());
	std::string attributeVariable = (boost::format(":%s") % name).str();
	AddExpressionAttributeValues(attributeVariable.c_str(), value);
	std::string setClause = (boost::format("%s = %s") % attributeName % attributeVariable).str();
	setClauses_.push_back(setClause);
}

void DynamoUpdateItem::setUpdateExpression()
{
	std::string updateExpression = "SET ";
	for (size_t i = 0; i < setClauses_.size(); i++) {
		updateExpression += setClauses_[i];
		if (i != setClauses_.size() - 1) {
			// There are more to come
			updateExpression += ", ";
		}
	}
	SetUpdateExpression(updateExpression.c_str());
}

DynamoQuery::DynamoQuery(std::string const &table, std::string const &keyName, std::string const &keyValue)
{
	SetTableName(table.c_str());
	std::string keyAttributeName = (boost::format("#%s") % keyName).str();
	AddExpressionAttributeNames(keyAttributeName.c_str(), keyName.c_str());
	SetKeyConditionExpression((boost::format("%s = :s") % keyAttributeName).str().c_str());
	DynamoDict attrs;
	attrs.addAttribute(":s", keyValue);
	SetExpressionAttributeValues(attrs);

	// The query is ready to be fired now
}

bool DynamoQuery::fetchResults(const Aws::DynamoDB::DynamoDBClient &dynamoClient, std::function<void(TDynamoMap &result)> resultHandler)
{
	// We might have to run this query multiple times in order to retrieve all results from the database
	bool more = true;
	while (more) {
		Aws::DynamoDB::Model::QueryOutcome result = dynamoClient.Query(*this);
		if (!result.IsSuccess()) {
			SimpleLogger::instance()->postMessage(toString(result.GetError().GetMessage()));
			return false;
		}

		// Use the result
		Aws::DynamoDB::Model::QueryResult patches = result.GetResult();
		for (auto item : patches.GetItems()) {
			resultHandler(item);
		}
		more = !patches.GetLastEvaluatedKey().empty();
		SetExclusiveStartKey(patches.GetLastEvaluatedKey());
	}
	return true;
}

DynamoDeleteItem::DynamoDeleteItem(std::string const &table, DynamoDict &keys)
{
	SetTableName(table.c_str());
	SetKey(keys);
}

bool DynamoDeleteItem::performDelete(const Aws::DynamoDB::DynamoDBClient &dynamoClient)
{
	auto outcome = dynamoClient.DeleteItem(*this);
	if (!outcome.IsSuccess()) {
		SimpleLogger::instance()->postMessage(toString(outcome.GetError().GetMessage()));
		return false;
	}
	return true;
}
