/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "sd-device.h"

#include "alloc-util.h"
#include "device-private.h"
#include "netif-naming-scheme.h"
#include "proc-cmdline.h"
#include "string-util.h"
#include "string-table.h"

#ifdef _DEFAULT_NET_NAMING_SCHEME_TEST
/* The primary purpose of this check is to verify that _DEFAULT_NET_NAMING_SCHEME_TEST
 * is a valid identifier. If an invalid name is given during configuration, this will
 * fail with a name error. */
assert_cc(_DEFAULT_NET_NAMING_SCHEME_TEST >= 0);
#endif

static const NamingScheme naming_schemes[] = {
        { "v238", NAMING_V238 },
        { "v239", NAMING_V239 },
        { "v240", NAMING_V240 },
        { "v241", NAMING_V241 },
        { "v243", NAMING_V243 },
        { "v245", NAMING_V245 },
        { "v247", NAMING_V247 },
        { "v249", NAMING_V249 },
        { "v250", NAMING_V250 },
        { "v251", NAMING_V251 },
        { "v252", NAMING_V252 },
        { "rhel-8.0", NAMING_RHEL_8_0 },
        { "rhel-8.1", NAMING_RHEL_8_1 },
        { "rhel-8.2", NAMING_RHEL_8_2 },
        { "rhel-8.3", NAMING_RHEL_8_3 },
        { "rhel-8.4", NAMING_RHEL_8_4 },
        { "rhel-8.5", NAMING_RHEL_8_5 },
        { "rhel-8.6", NAMING_RHEL_8_6 },
        { "rhel-8.7", NAMING_RHEL_8_7 },
        { "rhel-8.8", NAMING_RHEL_8_8 },
        { "rhel-8.9", NAMING_RHEL_8_9 },
        { "rhel-8.10", NAMING_RHEL_8_10 },
        { "rhel-9.0", NAMING_RHEL_9_0 },
        { "rhel-9.1", NAMING_RHEL_9_1 },
        { "rhel-9.2", NAMING_RHEL_9_2 },
        /* … add more schemes here, as the logic to name devices is updated … */

        EXTRA_NET_NAMING_MAP
};

const NamingScheme* naming_scheme_from_name(const char *name) {
        /* "latest" may either be defined explicitly by the extra map, in which case we will find it in
         * the table like any other name. After iterating through the table, we check for "latest" again,
         * which means that if not mapped explicitly, it maps to the last defined entry, whatever that is. */

        for (size_t i = 0; i < ELEMENTSOF(naming_schemes); i++)
                if (streq(naming_schemes[i].name, name))
                        return naming_schemes + i;

        if (streq(name, "latest"))
                return naming_schemes + ELEMENTSOF(naming_schemes) - 1;

        return NULL;
}

const NamingScheme* naming_scheme(void) {
        static const NamingScheme *cache = NULL;
        _cleanup_free_ char *buffer = NULL;
        const char *e, *k;

        if (cache)
                return cache;

        /* Acquire setting from the kernel command line */
        (void) proc_cmdline_get_key("net.naming-scheme", 0, &buffer);

        /* Also acquire it from an env var */
        e = getenv("NET_NAMING_SCHEME");
        if (e) {
                if (*e == ':') {
                        /* If prefixed with ':' the kernel cmdline takes precedence */
                        k = buffer ?: e + 1;
                } else
                        k = e; /* Otherwise the env var takes precedence */
        } else
                k = buffer;

        if (k) {
                cache = naming_scheme_from_name(k);
                if (cache) {
                        log_info("Using interface naming scheme '%s'.", cache->name);
                        return cache;
                }

                log_warning("Unknown interface naming scheme '%s' requested, ignoring.", k);
        }

        cache = naming_scheme_from_name(DEFAULT_NET_NAMING_SCHEME);
        assert(cache);
        log_info("Using default interface naming scheme '%s'.", cache->name);

        return cache;
}

static const char* const name_policy_table[_NAMEPOLICY_MAX] = {
        [NAMEPOLICY_KERNEL] = "kernel",
        [NAMEPOLICY_KEEP] = "keep",
        [NAMEPOLICY_DATABASE] = "database",
        [NAMEPOLICY_ONBOARD] = "onboard",
        [NAMEPOLICY_SLOT] = "slot",
        [NAMEPOLICY_PATH] = "path",
        [NAMEPOLICY_MAC] = "mac",
};

DEFINE_STRING_TABLE_LOOKUP(name_policy, NamePolicy);

static const char* const alternative_names_policy_table[_NAMEPOLICY_MAX] = {
        [NAMEPOLICY_DATABASE] = "database",
        [NAMEPOLICY_ONBOARD] = "onboard",
        [NAMEPOLICY_SLOT] = "slot",
        [NAMEPOLICY_PATH] = "path",
        [NAMEPOLICY_MAC] = "mac",
};

DEFINE_STRING_TABLE_LOOKUP(alternative_names_policy, NamePolicy);

static int naming_sysattr_allowed_by_default(sd_device *dev) {
        int r;

        assert(dev);

        r = device_get_property_bool(dev, "ID_NET_NAME_ALLOW");
        if (r == -ENOENT)
                return true;

        return r;
}

static int naming_sysattr_allowed(sd_device *dev, const char *sysattr) {
        char *sysattr_property;
        int r;

        assert(dev);
        assert(sysattr);

        sysattr_property = strjoina("ID_NET_NAME_ALLOW_", sysattr);
        ascii_strupper(sysattr_property);

        r = device_get_property_bool(dev, sysattr_property);
        if (r == -ENOENT)
                /* If ID_NET_NAME_ALLOW is not set or set to 1 default is to allow */
                return naming_sysattr_allowed_by_default(dev);

        return r;
}

int device_get_sysattr_int_filtered(sd_device *device, const char *sysattr, int *ret_value) {
        int r;

        r = naming_sysattr_allowed(device, sysattr);
        if (r < 0)
                return r;
        if (r == 0)
                return -ENOENT;

        return device_get_sysattr_int(device, sysattr, ret_value);
}

int device_get_sysattr_unsigned_filtered(sd_device *device, const char *sysattr, unsigned *ret_value) {
        int r;

        r = naming_sysattr_allowed(device, sysattr);
        if (r < 0)
                return r;
        if (r == 0)
                return -ENOENT;

        return device_get_sysattr_unsigned(device, sysattr, ret_value);
}

int device_get_sysattr_bool_filtered(sd_device *device, const char *sysattr) {
        int r;

        r = naming_sysattr_allowed(device, sysattr);
        if (r < 0)
                return r;
        if (r == 0)
                return -ENOENT;

        return device_get_sysattr_bool(device, sysattr);
}

int device_get_sysattr_value_filtered(sd_device *device, const char *sysattr, const char **ret_value) {
        int r;

        r = naming_sysattr_allowed(device, sysattr);
        if (r < 0)
                return r;
        if (r == 0)
                return -ENOENT;

        return sd_device_get_sysattr_value(device, sysattr, ret_value);
}
