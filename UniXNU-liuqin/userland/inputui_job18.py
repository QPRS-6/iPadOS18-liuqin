"""Retain the original22H374 InputUI launch-angel cache identity."""
from copy import deepcopy

LABEL = 'com.apple.InputUI'
DAEMON = '/System/Library/LaunchDaemons/com.apple.InputUI.plist'
ANGEL = '/System/Library/LaunchAngels/com.apple.InputUI.plist'


def preserves_metadata(before, after):
    """Preserve owner/group/mode and flags, allowing only decmpfs decompression.

    Writing a new plist into a compressed inode removes UF_COMPRESSED (0x20).
    Reapplying that representation flag without compressed data is invalid.
    No other flag or ownership change is accepted, nor adding compression.
    """
    if before[:3] != after[:3]:
        return False
    old, new = before[3], after[3]
    return old == new or (old & 0x20 != 0 and new == old & ~0x20)


def restore_inputui(cache):
    """Return a copied cache and the same job at its original native path."""
    if cache.get('VersionNumber') != 7 or not isinstance(cache.get('LaunchDaemons'), dict):
        raise ValueError('22H374 Version7 service cache required')
    entries = cache['LaunchDaemons']
    matches = [(path, job) for path, job in entries.items()
               if isinstance(job, dict) and job.get('Label') == LABEL]
    if len(matches) != 1:
        raise ValueError('Expected exactly one InputUI cache entry')
    path, job = matches[0]
    if path not in (DAEMON, ANGEL) or job.get('ProcessType') != 'App':
        raise ValueError('Unexpected InputUI identity')
    arguments = job.get('ProgramArguments')
    if not isinstance(arguments, list) or not arguments or arguments[0] != '/Applications/InputUI.app/InputUI':
        raise ValueError('Unexpected InputUI executable')
    result = deepcopy(cache)
    entry = result['LaunchDaemons'].pop(path)
    result['LaunchDaemons'][ANGEL] = entry
    return result, deepcopy(entry)
