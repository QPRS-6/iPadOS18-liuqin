"""Native22H374 keyboard architecture preference, independent of GPU policy."""
from copy import deepcopy


def local_keyboard(preferences):
    """Select UIKit's native non-OOP path, preserving other keyboard preferences."""
    if not isinstance(preferences, dict):
        raise ValueError('Expected a keyboard preference dictionary')
    result = deepcopy(preferences)
    result['KeyboardOOP'] = False
    return result


def add_local_keyboard_seed(skeleton):
    """Use the existing PID1 copy-if-absent seed path before UIKit caches OOP.

    An existing user preference domain remains untouched by copy_new. The
    native seed writer assigns mobile501:501 and0600 to the new domain.
    """
    if not isinstance(skeleton, str):
        raise ValueError('Expected a seed skeleton string')
    if len(skeleton.encode('utf8')) >= 65536:
        raise ValueError('Seed skeleton exceeds native PID1 buffer')
    target = '/private/var/mobile/Library/Preferences/com.apple.keyboard.plist'
    source = '/pong/var-seed/mobile/Library/Preferences/com.apple.keyboard.plist'
    entry = '\t'.join(('f', '0600', '501', '501', target, source))
    for line in skeleton.splitlines():
        fields = line.split('\t')
        if len(fields) >= 5 and fields[4] == target:
            if line != entry:
                raise ValueError('Existing keyboard domain seed differs')
            return skeleton
    result = skeleton + ('' if skeleton.endswith('\n') else '\n') + entry + '\n'
    if len(result.encode('utf8')) >= 65536:
        raise ValueError('Seed skeleton exceeds native PID1 buffer')
    return result
