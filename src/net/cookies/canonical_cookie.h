// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_COOKIES_CANONICAL_COOKIE_H_
#define NET_COOKIES_CANONICAL_COOKIE_H_

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/optional.h"
#include "base/time/time.h"
#include "net/base/net_export.h"
#include "net/cookies/cookie_access_result.h"
#include "net/cookies/cookie_constants.h"
#include "net/cookies/cookie_inclusion_status.h"
#include "net/cookies/cookie_options.h"

class GURL;

namespace net {

class ParsedCookie;
class CanonicalCookie;

struct CookieWithAccessResult;
struct CookieAndLineWithAccessResult;

using CookieList = std::vector<CanonicalCookie>;
using CookieAndLineAccessResultList =
    std::vector<CookieAndLineWithAccessResult>;
using CookieAccessResultList = std::vector<CookieWithAccessResult>;

class NET_EXPORT CanonicalCookie {
 public:
  using UniqueCookieKey = std::tuple<std::string, std::string, std::string>;

  CanonicalCookie();
  CanonicalCookie(const CanonicalCookie& other);

  // This constructor does not validate or canonicalize their inputs;
  // the resulting CanonicalCookies should not be relied on to be canonical
  // unless the caller has done appropriate validation and canonicalization
  // themselves.
  // NOTE: Prefer using CreateSanitizedCookie() over directly using this
  // constructor.
  CanonicalCookie(
      const std::string& name,
      const std::string& value,
      const std::string& domain,
      const std::string& path,
      const base::Time& creation,
      const base::Time& expiration,
      const base::Time& last_access,
      bool secure,
      bool httponly,
      CookieSameSite same_site,
      CookiePriority priority,
      CookieSourceScheme scheme_secure = CookieSourceScheme::kUnset);

  ~CanonicalCookie();

  // Supports the default copy constructor.

  // Creates a new |CanonicalCookie| from the |cookie_line| and the
  // |creation_time|.  Canonicalizes inputs.  May return nullptr if
  // an attribute value is invalid.  |url| must be valid.  |creation_time| may
  // not be null. Sets optional |status| to the relevant CookieInclusionStatus
  // if provided.  |server_time| indicates what the server sending us the Cookie
  // thought the current time was when the cookie was produced.  This is used to
  // adjust for clock skew between server and host.
  //
  // SameSite and HttpOnly related parameters are not checked here,
  // so creation of CanonicalCookies with e.g. SameSite=Strict from a cross-site
  // context is allowed. Create() also does not check whether |url| has a secure
  // scheme if attempting to create a Secure cookie. The Secure, SameSite, and
  // HttpOnly related parameters should be checked when setting the cookie in
  // the CookieStore.
  //
  // If a cookie is returned, |cookie->IsCanonical()| will be true.
  static std::unique_ptr<CanonicalCookie> Create(
      const GURL& url,
      const std::string& cookie_line,
      const base::Time& creation_time,
      base::Optional<base::Time> server_time,
      CookieInclusionStatus* status = nullptr);

  // Create a canonical cookie based on sanitizing the passed inputs in the
  // context of the passed URL.  Returns a null unique pointer if the inputs
  // cannot be sanitized.  If a cookie is created, |cookie->IsCanonical()|
  // will be true.
  static std::unique_ptr<CanonicalCookie> CreateSanitizedCookie(
      const GURL& url,
      const std::string& name,
      const std::string& value,
      const std::string& domain,
      const std::string& path,
      base::Time creation_time,
      base::Time expiration_time,
      base::Time last_access_time,
      bool secure,
      bool http_only,
      CookieSameSite same_site,
      CookiePriority priority);

  const std::string& Name() const { return name_; }
  const std::string& Value() const { return value_; }
  // We represent the cookie's host-only-flag as the absence of a leading dot in
  // Domain(). See IsDomainCookie() and IsHostCookie() below.
  // If you want the "cookie's domain" as described in RFC 6265bis, use
  // DomainWithoutDot().
  const std::string& Domain() const { return domain_; }
  const std::string& Path() const { return path_; }
  const base::Time& CreationDate() const { return creation_date_; }
  const base::Time& LastAccessDate() const { return last_access_date_; }
  bool IsPersistent() const { return !expiry_date_.is_null(); }
  const base::Time& ExpiryDate() const { return expiry_date_; }
  bool IsSecure() const { return secure_; }
  bool IsHttpOnly() const { return httponly_; }
  CookieSameSite SameSite() const { return same_site_; }
  CookiePriority Priority() const { return priority_; }
  // Returns an enum indicating the source scheme that set this cookie. This is
  // not part of the cookie spec but is being used to collect metrics for a
  // potential change to the cookie spec.
  CookieSourceScheme SourceScheme() const { return source_scheme_; }
  bool IsDomainCookie() const {
    return !domain_.empty() && domain_[0] == '.'; }
  bool IsHostCookie() const { return !IsDomainCookie(); }

  // Returns the cookie's domain, with the leading dot removed, if present.
  // This corresponds to the "cookie's domain" as described in RFC 6265bis.
  std::string DomainWithoutDot() const;

  bool IsExpired(const base::Time& current) const {
    return !expiry_date_.is_null() && current >= expiry_date_;
  }

  // Are the cookies considered equivalent in the eyes of RFC 2965.
  // The RFC says that name must match (case-sensitive), domain must
  // match (case insensitive), and path must match (case sensitive).
  // For the case insensitive domain compare, we rely on the domain
  // having been canonicalized (in
  // GetCookieDomainWithString->CanonicalizeHost).
  bool IsEquivalent(const CanonicalCookie& ecc) const {
    // It seems like it would make sense to take secure, httponly, and samesite
    // into account, but the RFC doesn't specify this.
    // NOTE: Keep this logic in-sync with TrimDuplicateCookiesForKey().
    return (name_ == ecc.Name() && domain_ == ecc.Domain()
            && path_ == ecc.Path());
  }

  // Returns a key such that two cookies with the same UniqueKey() are
  // guaranteed to be equivalent in the sense of IsEquivalent().
  UniqueCookieKey UniqueKey() const {
    return std::make_tuple(name_, domain_, path_);
  }

  // Checks a looser set of equivalency rules than 'IsEquivalent()' in order
  // to support the stricter 'Secure' behaviors specified in Step 12 of
  // https://tools.ietf.org/html/draft-ietf-httpbis-rfc6265bis-05#section-5.4
  // which originated from the proposal in
  // https://tools.ietf.org/html/draft-ietf-httpbis-cookie-alone#section-3
  //
  // Returns 'true' if this cookie's name matches |secure_cookie|, and this
  // cookie is a domain-match for |secure_cookie| (or vice versa), and
  // |secure_cookie|'s path is "on" this cookie's path (as per 'IsOnPath()').
  //
  // Note that while the domain-match cuts both ways (e.g. 'example.com'
  // matches 'www.example.com' in either direction), the path-match is
  // unidirectional (e.g. '/login/en' matches '/login' and '/', but
  // '/login' and '/' do not match '/login/en').
  //
  // Conceptually:
  // If new_cookie.IsEquivalentForSecureCookieMatching(secure_cookie) is true,
  // this means that new_cookie would "shadow" secure_cookie: they would would
  // be indistinguishable when serialized into a Cookie header. This is
  // important because, if an attacker is attempting to set new_cookie, it
  // should not be allowed to mislead the server into using new_cookie's value
  // instead of secure_cookie's.
  //
  // The reason for the asymmetric path comparison ("cookie1=bad; path=/a/b"
  // from an insecure source is not allowed if "cookie1=good; secure; path=/a"
  // exists, but "cookie2=bad; path=/a" from an insecure source is allowed if
  // "cookie2=good; secure; path=/a/b" exists) is because cookies in the Cookie
  // header are serialized with longer path first. (See CookieSorter in
  // cookie_monster.cc.) That is, they would be serialized as "Cookie:
  // cookie1=bad; cookie1=good" in one case, and "Cookie: cookie2=good;
  // cookie2=bad" in the other case. The first scenario is not allowed because
  // the attacker injects the bad value, whereas the second scenario is ok
  // because the good value is still listed first.
  bool IsEquivalentForSecureCookieMatching(
      const CanonicalCookie& secure_cookie) const;

  void SetSourceScheme(CookieSourceScheme source_scheme) {
    source_scheme_ = source_scheme;
  }
  void SetLastAccessDate(const base::Time& date) {
    last_access_date_ = date;
  }
  void SetCreationDate(const base::Time& date) { creation_date_ = date; }

  // Returns true if the given |url_path| path-matches this cookie's cookie-path
  // as described in section 5.1.4 in RFC 6265. This returns true if |path_| and
  // |url_path| are identical, or if |url_path| is a subdirectory of |path_|.
  bool IsOnPath(const std::string& url_path) const;

  // This returns true if this cookie's |domain_| indicates that it can be
  // accessed by |host|.
  //
  // In the case where |domain_| has no leading dot, this is a host cookie and
  // will only domain match if |host| is identical to |domain_|.
  //
  // In the case where |domain_| has a leading dot, this is a domain cookie. It
  // will match |host| if |domain_| is a suffix of |host|, or if |domain_| is
  // exactly equal to |host| plus a leading dot.
  //
  // Note that this isn't quite the same as the "domain-match" algorithm in RFC
  // 6265bis, since our implementation uses the presence of a leading dot in the
  // |domain_| string in place of the spec's host-only-flag. That is, if
  // |domain_| has no leading dot, then we only consider it matching if |host|
  // is identical (which reflects the intended behavior when the cookie has a
  // host-only-flag), whereas the RFC also treats them as domain-matching if
  // |domain_| is a subdomain of |host|.
  bool IsDomainMatch(const std::string& host) const;

  // Returns if the cookie should be included (and if not, why) for the given
  // request |url| using the CookieInclusionStatus enum. HTTP only cookies can
  // be filter by using appropriate cookie |options|. PLEASE NOTE that this
  // method does not check whether a cookie is expired or not!
  CookieAccessResult IncludeForRequestURL(
      const GURL& url,
      const CookieOptions& options,
      CookieAccessSemantics access_semantics =
          CookieAccessSemantics::UNKNOWN) const;

  // Returns if the cookie with given attributes can be set in context described
  // by |options|, and if no, describes why.
  // WARNING: this does not cover checking whether secure cookies are set in
  // a secure schema, since whether the schema is secure isn't part of
  // |options|.
  CookieAccessResult IsSetPermittedInContext(
      const CookieOptions& options,
      CookieAccessSemantics access_semantics =
          CookieAccessSemantics::UNKNOWN) const;

  // Overload that updates an existing |status| rather than returning a new one.
  void IsSetPermittedInContext(const CookieOptions& options,
                               CookieAccessSemantics access_semantics,
                               CookieAccessResult* access_result) const;

  std::string DebugString() const;

  static std::string CanonPathWithString(const GURL& url,
                                         const std::string& path_string);

  // Returns a "null" time if expiration was unspecified or invalid.
  static base::Time CanonExpiration(const ParsedCookie& pc,
                                    const base::Time& current,
                                    const base::Time& server_time);

  // Cookie ordering methods.

  // Returns true if the cookie is less than |other|, considering only name,
  // domain and path. In particular, two equivalent cookies (see IsEquivalent())
  // are identical for PartialCompare().
  bool PartialCompare(const CanonicalCookie& other) const;

  // Return whether this object is a valid CanonicalCookie().  Invalid
  // cookies may be constructed by the detailed constructor.
  // A cookie is considered canonical if-and-only-if:
  // * It can be created by CanonicalCookie::Create, or
  // * It is identical to a cookie created by CanonicalCookie::Create except
  //   that the creation time is null, or
  // * It can be derived from a cookie created by CanonicalCookie::Create by
  //   entry into and retrieval from a cookie store (specifically, this means
  //   by the setting of an creation time in place of a null creation time, and
  //   the setting of a last access time).
  // An additional requirement on a CanonicalCookie is that if the last
  // access time is non-null, the creation time must also be non-null and
  // greater than the last access time.
  bool IsCanonical() const;

  // Returns whether the effective SameSite mode is SameSite=None (i.e. no
  // SameSite restrictions).
  bool IsEffectivelySameSiteNone(CookieAccessSemantics access_semantics =
                                     CookieAccessSemantics::UNKNOWN) const;

  CookieEffectiveSameSite GetEffectiveSameSiteForTesting(
      CookieAccessSemantics access_semantics =
          CookieAccessSemantics::UNKNOWN) const;

  // Returns the cookie line (e.g. "cookie1=value1; cookie2=value2") represented
  // by |cookies|. The string is built in the same order as the given list.
  static std::string BuildCookieLine(const CookieList& cookies);

  // Same as above but takes a CookieAccessResultList
  // (ignores the access result).
  static std::string BuildCookieLine(const CookieAccessResultList& cookies);

 private:
  FRIEND_TEST_ALL_PREFIXES(CanonicalCookieTest, TestPrefixHistograms);

  // The special cookie prefixes as defined in
  // https://tools.ietf.org/html/draft-west-cookie-prefixes
  //
  // This enum is being histogrammed; do not reorder or remove values.
  enum CookiePrefix {
    COOKIE_PREFIX_NONE = 0,
    COOKIE_PREFIX_SECURE,
    COOKIE_PREFIX_HOST,
    COOKIE_PREFIX_LAST
  };

  // Returns the CookiePrefix (or COOKIE_PREFIX_NONE if none) that
  // applies to the given cookie |name|.
  static CookiePrefix GetCookiePrefix(const std::string& name);
  // Records histograms to measure how often cookie prefixes appear in
  // the wild and how often they would be blocked.
  static void RecordCookiePrefixMetrics(CookiePrefix prefix,
                                        bool is_cookie_valid);
  // Returns true if a prefixed cookie does not violate any of the rules
  // for that cookie.
  static bool IsCookiePrefixValid(CookiePrefix prefix,
                                  const GURL& url,
                                  const ParsedCookie& parsed_cookie);
  static bool IsCookiePrefixValid(CookiePrefix prefix,
                                  const GURL& url,
                                  bool secure,
                                  const std::string& domain,
                                  const std::string& path);

  // Returns the effective SameSite mode to apply to this cookie. Depends on the
  // value of the given SameSite attribute and whether the
  // SameSiteByDefaultCookies feature is enabled, as well as the access
  // semantics of the cookie.
  // Note: If you are converting to a different representation of a cookie, you
  // probably want to use SameSite() instead of this method. Otherwise, if you
  // are considering using this method, consider whether you should use
  // IncludeForRequestURL() or IsSetPermittedInContext() instead of doing the
  // SameSite computation yourself.
  CookieEffectiveSameSite GetEffectiveSameSite(
      CookieAccessSemantics access_semantics) const;

  // Returns whether the cookie was created at most |age_threshold| ago.
  bool IsRecentlyCreated(base::TimeDelta age_threshold) const;

  std::string name_;
  std::string value_;
  std::string domain_;
  std::string path_;
  base::Time creation_date_;
  base::Time expiry_date_;
  base::Time last_access_date_;
  bool secure_;
  bool httponly_;
  CookieSameSite same_site_;
  CookiePriority priority_;
  CookieSourceScheme source_scheme_;
};

// Used to pass excluded cookie information when it's possible that the
// canonical cookie object may not be available.
struct NET_EXPORT CookieAndLineWithAccessResult {
  CookieAndLineWithAccessResult();
  CookieAndLineWithAccessResult(base::Optional<CanonicalCookie> cookie,
                                std::string cookie_string,
                                CookieAccessResult access_result);
  CookieAndLineWithAccessResult(
      const CookieAndLineWithAccessResult& cookie_and_line_with_access_result);

  CookieAndLineWithAccessResult& operator=(
      const CookieAndLineWithAccessResult& cookie_and_line_with_access_result);

  CookieAndLineWithAccessResult(
      CookieAndLineWithAccessResult&& cookie_and_line_with_access_result);

  ~CookieAndLineWithAccessResult();

  base::Optional<CanonicalCookie> cookie;
  std::string cookie_string;
  CookieAccessResult access_result;
};

struct CookieWithAccessResult {
  CanonicalCookie cookie;
  CookieAccessResult access_result;
};

}  // namespace net

#endif  // NET_COOKIES_CANONICAL_COOKIE_H_
