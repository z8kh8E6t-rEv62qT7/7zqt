#import "BrokerService.h"

#import "BrokerServiceUtilities.h"

@implementation BrokerService (Menu)

- (Z7BrokerMenuPlan *)buildMenuPlanOnQueueWithPaths:(NSArray<NSString *> *)paths locale:(NSString *)locale {
    if (_invalidated || _session == nullptr) {
        return [[Z7BrokerMenuPlan alloc] initWithOK:NO
                                             status:Z7_MI_STATUS_INTERNAL_ERROR
                                       errorMessage:@"Broker service is not available."
                                        menuVisible:NO
                                            actions:@[]];
    }

    Z7CStringArray selectedPaths(paths);
    std::string localeHint = Z7ToStdString(locale);
    z7_mi_selection_t selection = {
        selectedPaths.data(),
        selectedPaths.size(),
        false,
        nullptr,
        locale == nil ? nullptr : localeHint.c_str()
    };

    z7_mi_menu_plan_t plan = {};
    z7_mi_status_t status = z7_mi_build_menu_plan(_session, &selection, &plan);
    NSMutableArray<Z7BrokerMenuAction *> *actions = [NSMutableArray arrayWithCapacity:plan.action_count];
    if (plan.actions != nullptr) {
        for (size_t index = 0; index < plan.action_count; ++index) {
            const z7_mi_menu_action_t &action = plan.actions[index];
            if (action.action_id == nullptr || action.title == nullptr) {
                continue;
            }
            [actions addObject:[[Z7BrokerMenuAction alloc] initWithActionID:Z7ToNSString(action.action_id)
                                                                      title:Z7ToNSString(action.title)]];
        }
    }

    Z7BrokerMenuPlan *dto = [[Z7BrokerMenuPlan alloc] initWithOK:status == Z7_MI_STATUS_OK && plan.ok
                                                          status:status
                                                    errorMessage:Z7NullableNSString(plan.error_message)
                                                     menuVisible:plan.menu_visible
                                                         actions:actions];
    z7_mi_free_menu_plan(&plan);
    return dto;
}

- (Z7BrokerActionResult *)runMenuActionOnQueueWithActionID:(NSString *)actionID
                                                     paths:(NSArray<NSString *> *)paths
                                                    locale:(NSString *)locale {
    if (_invalidated || _session == nullptr) {
        return [[Z7BrokerActionResult alloc] initWithOK:NO
                                                 status:Z7_MI_STATUS_INTERNAL_ERROR
                                           errorMessage:@"Broker service is not available."
                                               actionID:actionID ?: @""];
    }

    Z7CStringArray selectedPaths(paths);
    std::string action = Z7ToStdString(actionID);
    std::string localeHint = Z7ToStdString(locale);
    z7_mi_selection_t selection = {
        selectedPaths.data(),
        selectedPaths.size(),
        false,
        nullptr,
        locale == nil ? nullptr : localeHint.c_str()
    };

    z7_mi_action_result_t result = {};
    z7_mi_status_t status = z7_mi_execute_menu_action(_session, action.c_str(), &selection, &result);
    NSString *resultActionID = result.action_id == nullptr ? actionID ?: @"" : Z7ToNSString(result.action_id);
    Z7BrokerActionResult *dto = [[Z7BrokerActionResult alloc] initWithOK:status == Z7_MI_STATUS_OK && result.ok
                                                                  status:status
                                                            errorMessage:Z7NullableNSString(result.error_message)
                                                                actionID:resultActionID];
    z7_mi_free_action_result(&result);
    return dto;
}

@end
