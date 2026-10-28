// Rec Cue! for macOS — the one thing Swift cannot do: catch an Objective-C exception.
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// Runs `block`. An Objective-C exception thrown inside it is caught, and its name and reason come back (nil: none).
/// Only for the window's text, which macOS's text drawing very rarely throws from (see Sources/RecCueObjC/DEVELOPMENT.md).
NSString *_Nullable RCCatch(NS_NOESCAPE void (^block)(void));

NS_ASSUME_NONNULL_END
