#import "BrokerServiceUtilities.h"

NSString *Z7ToNSString(const char *value) {
    if (value == nullptr) {
        return @"";
    }
    NSString *result = [NSString stringWithUTF8String:value];
    return result == nil ? @"" : result;
}

NSString *Z7NullableNSString(const char *value) {
    if (value == nullptr) {
        return nil;
    }
    return Z7ToNSString(value);
}

std::string Z7ToStdString(NSString *value) {
    if (value == nil) {
        return {};
    }
    const char *utf8 = [value UTF8String];
    return utf8 == nullptr ? std::string() : std::string(utf8);
}

Z7CStringArray::Z7CStringArray(NSArray<NSString *> *values) {
    _storage.reserve(values.count);
    _pointers.reserve(values.count);
    for (NSString *value in values) {
        _storage.push_back(Z7ToStdString(value));
    }
    for (const std::string &value : _storage) {
        _pointers.push_back(value.c_str());
    }
}

const char *const *Z7CStringArray::data() const {
    return _pointers.empty() ? nullptr : _pointers.data();
}

size_t Z7CStringArray::size() const {
    return _pointers.size();
}
