/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <JuceHeader.h>

#include <aws/core/Aws.h>
#include <aws/core/utils/Outcome.h> 

#include <aws/dynamodb/DynamoDBClient.h>
#include <aws/dynamodb/model/UpdateItemRequest.h>
#include <aws/dynamodb/model/DeleteItemRequest.h>
#include <aws/dynamodb/model/QueryRequest.h>

typedef Aws::DynamoDB::Model::AttributeValue TDynamoValue;
typedef Aws::Map<Aws::String, TDynamoValue> TDynamoMap;

class DynamoDict : public TDynamoMap {
public:
	void addAttribute(std::string const &name, std::string const &value);
	void addAttribute(std::string const &name, int const value);
	void addAttribute(std::string const &name, std::vector<uint8> const &value);
};

class DynamoUpdateItem : public Aws::DynamoDB::Model::UpdateItemRequest {
public:
	DynamoUpdateItem(std::string const &table, std::set<const char *> const &keyNames);

	void addDict(DynamoDict &dict);
	
	void addUpdateAttribute(std::string const &name, TDynamoValue const &value);
	void addUpdateAttribute(std::string const &name, std::string const &value);

	void setUpdateExpression();

private:
	std::set<const char *> keyNames_;
	std::vector<std::string> setClauses_;
};

class DynamoQuery : public Aws::DynamoDB::Model::QueryRequest {
public:
	DynamoQuery(std::string const &table, std::string const &keyName, std::string const &keyValue);

	bool fetchResults(const Aws::DynamoDB::DynamoDBClient &dynamoClient, std::function<void(TDynamoMap &result)> resultHandler);
};

class DynamoDeleteItem : public Aws::DynamoDB::Model::DeleteItemRequest {
public:
	DynamoDeleteItem(std::string const &table, DynamoDict &keys);
	bool performDelete(const Aws::DynamoDB::DynamoDBClient &dynamoClient);
};

// Base Helper functions
std::string toString(Aws::String const &aws);
int toInteger(Aws::String const &aws);
bool getStringIfSet(Aws::Map<Aws::String, Aws::DynamoDB::Model::AttributeValue> &dbresult, const char *key, std::string &outString);
bool getBufferIfSet(Aws::Map<Aws::String, Aws::DynamoDB::Model::AttributeValue> &dbresult, const char *key, std::vector<uint8> &outBuffer);
bool getNumberIfSet(Aws::Map<Aws::String, Aws::DynamoDB::Model::AttributeValue> &dbresult, const char *key, int &out);
bool getBoolIfSet(Aws::Map<Aws::String, Aws::DynamoDB::Model::AttributeValue> &dbresult, const char *key, bool &out);
bool getStringSetIfSet(Aws::Map<Aws::String, Aws::DynamoDB::Model::AttributeValue> &dbresult, const char *key, std::vector<std::string> &out);

// Higher Level functions
TDynamoValue stringAttribute(std::string const &value);