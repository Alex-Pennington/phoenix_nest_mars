// The form calls on a running station (forms.h): form_list, form_get,
// form_check, form_submit and the form_template_* calls.
#pragma once

#include <map>
#include <optional>
#include <string>

namespace pna {
class Core;

// This station's own templates: DB_DIR/CALL.forms (beside CALL.db).
std::string forms_dir(const Core& core);

std::optional<std::string> handle_forms(Core& core, const std::string& cmd, std::map<std::string, std::string>& req);
}  // namespace pna
