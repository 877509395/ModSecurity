/*
 * ModSecurity, http://www.modsecurity.org/
 * Copyright (c) 2015 Trustwave Holdings, Inc. (http://www.trustwave.com/)
 *
 * You may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * If any of the files related to licensing are missing or if you have any
 * other questions related to licensing please contact Trustwave Holdings, Inc.
 * directly using the email address security@modsecurity.org.
 *
 */

#include "src/rule.h"

#include <stdio.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <cstring>
#include <list>
#include <utility>

#include "operators/operator.h"
#include "actions/action.h"
#include "modsecurity/modsecurity.h"
#include "actions/transformations/none.h"
#include "variables/variations/exclusion.h"
#include "src/utils.h"
#include "modsecurity/rules.h"
#include "src/macro_expansion.h"

using ModSecurity::Variables::Variations::Exclusion;

namespace ModSecurity {

using operators::Operator;
using actions::Action;
using Variables::Variable;
using actions::transformations::None;

Rule::~Rule() {
    if (op != NULL) {
        delete op;
    }
    while (actions_conf.empty() == false) {
        auto *a = actions_conf.back();
        actions_conf.pop_back();
        delete a;
    }
    while (actions_runtime_pre.empty() == false) {
        auto *a = actions_runtime_pre.back();
        actions_runtime_pre.pop_back();
        delete a;
    }
    while (actions_runtime_pos.empty() == false) {
        auto *a = actions_runtime_pos.back();
        actions_runtime_pos.pop_back();
        delete a;
    }
    while (variables != NULL && variables->empty() == false) {
        auto *a = variables->back();
        variables->pop_back();
        delete a;
    }

    if (variables != NULL) {
        delete variables;
    }
}

Rule::Rule(std::string marker)
    : chained(false),
    chainedRule(NULL),
    variables(NULL),
    op(NULL),
    rule_id(0),
    phase(-1),
    m_unconditional(false),
    m_secmarker(true),
    m_marker(marker),
    m_referenceCount(0),
    m_fileName(""),
    m_lineNumber(0) { }

Rule::Rule(Operator *_op,
        std::vector<Variable *> *_variables,
        std::vector<Action *> *actions,
        std::string fileName,
        int lineNumber): chained(false),
    chainedRule(NULL),
    variables(_variables),
    op(_op),
    rule_id(0),
    phase(-1),
    m_unconditional(false),
    m_secmarker(false),
    m_marker(""),
    m_referenceCount(0),
    m_fileName(fileName),
    m_lineNumber(lineNumber) {
    if (actions != NULL) {
        for (Action *a : *actions) {
            if (a->action_kind == Action::ConfigurationKind) {
                actions_conf.push_back(a);
                a->evaluate(this, NULL);
            } else if (a->action_kind
                == Action::RunTimeBeforeMatchAttemptKind) {
                actions_runtime_pre.push_back(a);
            } else if (a->action_kind == Action::RunTimeOnlyIfMatchKind) {
                actions_runtime_pos.push_back(a);
            } else {
                std::cout << "General failure, action: " << a->name;
                std::cout << " has an unknown type." << std::endl;
                delete a;
            }
        }
    }
    /**
     * If phase is not entered, we assume phase 2. For historical reasons.
     *
     */
    if (phase == -1) {
        phase = ModSecurity::Phases::RequestHeadersPhase;
    }

    if (op == NULL) {
        m_unconditional = true;
    }

    delete actions;
}


bool Rule::evaluateActions(Assay *assay) {
    int none = 0;
    bool containsDisruptive = false;
    // int transformations = 0;
    for (Action *a : this->actions_runtime_pre) {
        if (a->m_isNone) {
            none++;
        }
    }

#ifndef NO_LOGS
    assay->debug(4, "Running unconditional rule.");
#endif

    if (none == 0) {
        /*
        for (Action *a : assay->m_rules->defaultActions[this->phase]) {
            if (a->action_kind == actions::Action::RunTimeBeforeMatchAttemptKind) {
                value = a->evaluate(value, assay);
                assay->debug(9, "(SecDefaultAction) T (" + \
                    std::to_string(transformations) + ") " + \
                    a->name + ": \"" + value +"\"");
                    transformations++;
            }
        }
        */
    }

    for (Action *a : this->actions_runtime_pre) {
        /*
        if (none == 0) {
            value = a->evaluate(value, assay);
            assay->debug(9, " T (" + \
                std::to_string(transformations) + ") " + \
                a->name + ": \"" + value +"\"");
                transformations++;
        }
        */
        if (a->m_isNone) {
            none--;
        }
    }

    for (Action *a : this->actions_runtime_pos) {
        if (a->isDisruptive() == false) {
#ifndef NO_LOGS
            assay->debug(4, "Running (_non_ disruptive) action: " + a->action);
#endif
            a->evaluate(this, assay);
        } else {
            containsDisruptive = true;
        }
    }

    for (Action *a : assay->m_rules->defaultActions[this->phase]) {
        if (a->action_kind == actions::Action::RunTimeOnlyIfMatchKind) {
            if (a->isDisruptive()) {
                if (containsDisruptive) {
#ifndef NO_LOGS
                    assay->debug(4, "(SecDefaultAction) " \
                        "_ignoring_ action: " + a->action + \
                        " (rule contains a disruptive action)");
#endif
                } else {
                    if (assay->m_rules->secRuleEngine
                        == Rules::EnabledRuleEngine) {
#ifndef NO_LOGS
                        assay->debug(4, "(SecDefaultAction) " \
                            "Running action: " + a->action + \
                            " (rule _does not_ contains a " \
                            "disruptive action)");
#endif
                            a->evaluate(this, assay);
                    } else {
#ifndef NO_LOGS
                        assay->debug(4, "(SecDefaultAction) " \
                            "_Not_ running action: " + a->action + \
                            ". Rule _does not_ contains a " \
                            "disruptive action, but SecRuleEngine is not On.");
#endif
                    }
                }
            } else {
#ifndef NO_LOGS
                assay->debug(4, "(SecDefaultAction) Running action: " + \
                    a->action);
                    a->evaluate(this, assay);
#endif
            }
        }
    }

    for (Action *a : this->actions_runtime_pos) {
        if (a->isDisruptive()
            && assay->m_rules->secRuleEngine == Rules::EnabledRuleEngine) {
#ifndef NO_LOGS
            assay->debug(4, "Running (disruptive) action: " + a->action);
#endif
            a->evaluate(this, assay);
        } else if (a->isDisruptive()) {
#ifndef NO_LOGS
            assay->debug(4, "Not running disruptive action: " + \
                a->action + ". SecRuleEngine is not On");
#endif
        }
    }

    return true;
}


bool Rule::evaluate(Assay *assay) {
    bool ret = false;
    std::vector<Variable *> *variables = this->variables;

    if (m_secmarker == true) {
        return true;
    }
    if (m_unconditional == true) {
        return evaluateActions(assay);
    }

#ifndef NO_LOGS
    std::string eparam = MacroExpansion::expand(this->op->param, assay);

    if (this->op->param != eparam) {
        eparam = "\"" + eparam + "\" Was: \"" + this->op->param + "\"";
    } else {
        eparam = "\"" + eparam + "\"";
    }

    assay->debug(4, "(Rule: " + std::to_string(rule_id) \
        + ") Executing operator \"" + this->op->op \
        + "\" with param " \
        + eparam \
        + " against " \
        + Variable::to_s(variables) + ".");

    clock_t begin = clock();
#endif

    std::list<std::string> exclusions;
    for (int i = 0; i < variables->size(); i++) {
        Variable *variable = variables->at(i);
        if (variable->m_isExclusion) {
            std::vector<const transaction::Variable *> z;
            variable->evaluateInternal(assay, &z);
            for (auto &y : z) {
                exclusions.push_back(y->m_key);
            }
            exclusions.push_back(variable->m_name);
        }
    }

    for (int i = 0; i < variables->size(); i++) {
        int transformations = 0;
        Variable *variable = variables->at(i);
        if (variable->m_isExclusion) {
            continue;
        }

        std::vector<const transaction::Variable *> e;
        variable->evaluateInternal(assay, &e);

        for (auto &v : e) {
            if (std::find(exclusions.begin(), exclusions.end(),
                v->m_key) != exclusions.end()) {
#ifndef NO_LOGS
                assay->debug(9, "Variable: " + v->m_key + " is part of the" +
                    " exclusion list, skipping...");
#endif
                continue;
            }
            std::string value = v->m_value;
            int none = 0;
            for (Action *a : this->actions_runtime_pre) {
                if (a->m_isNone) {
                    none++;
                }
            }

            // Check for transformations on the SecDefaultAction
            // Notice that first we make sure that won't be a t:none
            // on the target rule.
            if (none == 0) {
                for (Action *a : assay->m_rules->defaultActions[this->phase]) {
                    if (a->action_kind ==
                        actions::Action::RunTimeBeforeMatchAttemptKind) {
                        value = a->evaluate(value, assay);
#ifndef NO_LOGS
                        assay->debug(9, "(SecDefaultAction) T (" + \
                            std::to_string(transformations) + ") " + \
                            a->name + ": \"" + value +"\"");
#endif
                        transformations++;
                    }
                }
            }

            for (Action *a : this->actions_runtime_pre) {
                if (none == 0) {
                    value = a->evaluate(value, assay);
#ifndef NO_LOGS
                    assay->debug(9, " T (" + \
                            std::to_string(transformations) + ") " + \
                            a->name + ": \"" + value +"\"");
#endif
                    transformations++;
                }
                if (a->m_isNone) {
                    none--;
                }
            }

#ifndef NO_LOGS
            assay->debug(9, "Target value: \"" + limitTo(80,
                toHexIfNeeded(value)) + "\" (Variable: " + v->m_key + ")");
#endif

            ret = this->op->evaluate(assay, value);

#ifndef NO_LOGS
            clock_t end = clock();
            double elapsed_secs = static_cast<double>(end - begin) \
                / CLOCKS_PER_SEC;

            assay->debug(4, "Operator completed in " + \
                std::to_string(elapsed_secs) + " seconds");
#endif

            if (ret) {
                bool containsDisruptive = false;
                bool chainResult = false;
#ifndef NO_LOGS
                assay->debug(4, "Rule returned 1.");
#endif

                for (Action *a :
                    this->actions_runtime_pos) {
                    if (a->isDisruptive() == false) {
#ifndef NO_LOGS
                        assay->debug(4, "Running (_non_ disruptive) action: " \
                            + a->action);
#endif
                        a->evaluate(this, assay);
                    } else {
                        containsDisruptive = true;
                    }
                }

                if (this->chained && this->chainedRule == NULL) {
#ifndef NO_LOGS
                    assay->debug(4, "Rule is marked as chained but there " \
                        "isn't a subsequent rule.");
#endif
                }
                if (this->chained && this->chainedRule != NULL) {
#ifndef NO_LOGS
                    assay->debug(4, "Executing chained rule.");
#endif
                    if (assay->m_collections.storeOrUpdateFirst("MATCHED_VAR",
                        value) == false) {
                        assay->m_collections.store("MATCHED_VAR", value);
                    }
                    if (assay->m_collections.storeOrUpdateFirst(
                        "MATCHED_VAR_NAME", v->m_key) == false) {
                        assay->m_collections.store("MATCHED_VAR_NAME",
                            v->m_key);
                    }
                    assay->m_collections.store("MATCHED_VARS:"
                        + v->m_key, value);
                    assay->m_collections.store("MATCHED_VARS_NAMES:"
                        + v->m_key, v->m_key);
                    chainResult = this->chainedRule->evaluate(assay);
                    assay->m_collections.storeOrUpdateFirst("MATCHED_VAR", "");
                    assay->m_collections.del("MATCHED_VARS:" + v->m_key);
                    assay->m_collections.del("MATCHED_VARS_NAMES:" + v->m_key);
                    assay->m_collections.del("MATCHED_VARS_NAME");
                }
                if (this->chained && chainResult == true || !this->chained) {
                    for (Action *a :
                        assay->m_rules->defaultActions[this->phase]) {
                        if (a->action_kind
                            == actions::Action::RunTimeOnlyIfMatchKind) {
                            if (a->isDisruptive()) {
                                if (containsDisruptive) {
#ifndef NO_LOGS
                                    assay->debug(4, "(SecDefaultAction) " \
                                        "_ignoring_ action: " + a->action \
                                        + " (rule contains " \
                                        + "a disruptive action)");
#endif
                                } else {
                                    if (assay->m_rules->secRuleEngine
                                        == Rules::EnabledRuleEngine) {
#ifndef NO_LOGS
                                        assay->debug(4, "(SecDefaultAction) " \
                                            "Running action: " + a->action + \
                                            " (rule _does not_ contains a " \
                                            "disruptive action)");
#endif
                                        a->evaluate(this, assay);
                                    } else {
#ifndef NO_LOGS
                                        assay->debug(4, "(SecDefaultAction) " \
                                            "_Not_ running action: " \
                                            + a->action + ". Rule _does not_" \
                                            + " contains a disruptive action,"\
                                            + " but SecRuleEngine is not On.");
#endif
                                    }
                                }
                            } else {
#ifndef NO_LOGS
                                assay->debug(4, "(SecDefaultAction) Running " \
                                    "action: " + a->action + "!!" \
                                    + std::to_string(a->isDisruptive()));
#endif
                                a->evaluate(this, assay);
                            }
                        }
                    }
                    for (Action *a :
                        this->actions_runtime_pos) {
                        if (a->isDisruptive()
                            && assay->m_rules->secRuleEngine
                                == Rules::EnabledRuleEngine) {
#ifndef NO_LOGS
                            assay->debug(4, "Running (disruptive) action: " + \
                                a->action);
#endif
                            a->evaluate(this, assay);
                        } else if (a->isDisruptive()) {
#ifndef NO_LOGS
                            assay->debug(4,
                                "Not running disruptive action: " + \
                                a->action + ". SecRuleEngine is not On");
#endif
                        }
                    }
                }

            } else {
#ifndef NO_LOGS
                assay->debug(4, "Rule returned 0.");
#endif
            }
        }

        while (e.empty() == false) {
            delete e.back();
            e.pop_back();
        }
    }
    return ret;
}

}  // namespace ModSecurity