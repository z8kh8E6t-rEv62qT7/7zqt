#import <Foundation/Foundation.h>

#include <cstddef>
#include <string>
#include <vector>

NSString *Z7ToNSString(const char *value);
NSString *Z7NullableNSString(const char *value);
std::string Z7ToStdString(NSString *value);

class Z7CStringArray {
public:
    explicit Z7CStringArray(NSArray<NSString *> *values);

    const char *const *data() const;
    size_t size() const;

private:
    std::vector<std::string> _storage;
    std::vector<const char *> _pointers;
};
