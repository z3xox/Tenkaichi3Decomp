"""Format arguments that translations must preserve, in the order printf reads them."""
import collections
import re

PRINTF = re.compile(r"%(?:%|[-+ #0']*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcspn])")
NAMED = re.compile(r"\{[A-Za-z_][A-Za-z_0-9]*\}")


def slots(text):
    return (PRINTF.findall(text), PRINTF.sub('', text).count('%'), collections.Counter(NAMED.findall(text)))
