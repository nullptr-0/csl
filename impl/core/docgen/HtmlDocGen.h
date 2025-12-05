#pragma once

#ifndef HTML_DOC_GEN_H
#define HTML_DOC_GEN_H

#include <unordered_map>
#include <string>
#include <memory>
#include <vector>
#include "../shared/CslRepresentation.h"

namespace CSL {
std::unordered_map<std::string, std::string> toHtmlDoc(const std::shared_ptr<CSL::ConfigSchema>& schema);
std::unordered_map<std::string, std::string> toHtmlDoc(const std::vector<std::shared_ptr<CSL::ConfigSchema>>& schemas);
}

#endif // HTML_DOC_GEN_H
