// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "fileloader.h"

#include <stack>

namespace OTB {

constexpr Identifier wildcard = {{'\0', '\0', '\0', '\0'}};

Loader::Loader(const std::string& fileName, const Identifier& acceptedIdentifier)
{
	std::error_code error;
	fileContents.map(fileName, error);
	if (error) {
		throw InvalidOTBFormat{};
	}

	constexpr auto minimalSize = sizeof(Identifier) + sizeof(Node::START) + sizeof(Node::type) + sizeof(Node::END);
	if (fileContents.size() <= minimalSize) {
		throw InvalidOTBFormat{};
	}

	Identifier fileIdentifier;
	std::copy(fileContents.data(), fileContents.data() + fileIdentifier.size(), fileIdentifier.begin());
	if (fileIdentifier != acceptedIdentifier && fileIdentifier != wildcard) {
		throw InvalidOTBFormat{};
	}
}

using NodeStack = std::stack<Node*, std::vector<Node*>>;
static Node& getCurrentNode(const NodeStack& nodeStack)
{
	if (nodeStack.empty()) {
		throw InvalidOTBFormat{};
	}
	return *nodeStack.top();
}

const Node& Loader::parseTree()
{
	const auto contentEnd = fileContents.data() + fileContents.size();
	auto it = fileContents.data() + sizeof(Identifier);
	if (static_cast<uint8_t>(*it) != Node::START) {
		throw InvalidOTBFormat{};
	}
	root.type = *(++it);
	root.propsBegin = ++it;
	NodeStack parseStack;
	parseStack.push(&root);

	for (; it != contentEnd; ++it) {
		switch (static_cast<uint8_t>(*it)) {
			case Node::START: {
				auto& currentNode = getCurrentNode(parseStack);
				if (currentNode.children.empty()) {
					currentNode.propsEnd = it;
				}
				currentNode.children.emplace_back();
				auto& child = currentNode.children.back();
				if (++it == contentEnd) {
					throw InvalidOTBFormat{};
				}
				child.type = *it;
				child.propsBegin = it + sizeof(Node::type);
				parseStack.push(&child);
				break;
			}
			case Node::END: {
				auto& currentNode = getCurrentNode(parseStack);
				if (currentNode.children.empty()) {
					currentNode.propsEnd = it;
				}
				parseStack.pop();
				break;
			}
			case Node::ESCAPE: {
				if (++it == contentEnd) {
					throw InvalidOTBFormat{};
				}
				break;
			}
			default: {
				break;
			}
		}
	}
	if (!parseStack.empty()) {
		throw InvalidOTBFormat{};
	}

	return root;
}

bool Loader::getProps(const Node& node, PropStream& props)
{
	auto size = std::distance(node.propsBegin, node.propsEnd);
	if (size == 0) {
		return false;
	}

	// Fast path: no ESCAPE bytes, so the props can be read in place from the
	// memory-mapped file without unescaping into propBuffer. The resulting
	// PropStream (and any string_view obtained from it) aliases the mapped
	// file and is only valid while this Loader is alive; callers must copy
	// what they need to keep (all current callers do).
	const char* escapePtr = static_cast<const char*>(std::memchr(node.propsBegin, Node::ESCAPE, size));
	if (!escapePtr) {
		props.init(node.propsBegin, size);
		return true;
	}

	// Slow path: unescape into propBuffer. An ESCAPE (0xFD) byte marks the
	// next byte as a literal. Copy escape-free runs in bulk and handle only
	// the escaped bytes individually.
	propBuffer.resize(size);
	char* dst = propBuffer.data();
	const char* src = node.propsBegin;
	const char* const srcEnd = node.propsEnd;
	const char* esc = escapePtr; // first escape, already located by memchr above

	do {
		dst = std::copy(src, esc, dst); // clean run before the escape
		src = esc + 1;                  // skip the ESCAPE marker
		if (src == srcEnd) {
			break; // trailing lone ESCAPE: nothing to keep
		}
		*dst++ = *src++; // escaped byte kept literally (may itself be 0xFD)
		esc = static_cast<const char*>(std::memchr(src, Node::ESCAPE, srcEnd - src));
	} while (esc);

	dst = std::copy(src, srcEnd, dst); // tail after the last escape (no-op on break)
	props.init(propBuffer.data(), std::distance(propBuffer.data(), dst));
	return true;
}

} // namespace OTB
