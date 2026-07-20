# VideoLAN Security Policy

VideoLAN is a volunteer-run non-profit, and its projects are free software. This page explains
what VideoLAN treats as security issues, where reports should be sent, how VideoLAN handles them,
and how reporters are credited. The table below identifies the correct reporting channel.

> **There are no bug bounties.** VideoLAN is a non-profit and cannot pay for reports. VideoLAN
> offers a fast, honest process and public credit.

## TL;DR

- The private channel is for any security issue affecting a supported release target, and for any
  report whose security risk is uncertain; use the project's confidential tracker (preferred) or
  [security@videolan.org](mailto:security@videolan.org). The public channel is for everything else:
  ordinary bugs and confirmed bugs not affecting any supported release target.
- The public channel has no confidentiality period. Private-channel reports become public 30 days
  after every affected supported release target has received a fixing release. If any affected
  supported release target remains unfixed after 90 days, the reporter may disclose.
- Confirmed security issues are high-risk only when both exploitability and significant security
  impact are demonstrated; all other confirmed security issues are low-risk. Low-risk fixes are
  public, without an embargo, security bulletin, or VideoLAN CVE request. High-risk issues are
  subject to coordinated disclosure.
- A controlled proof is sufficient; a weaponized end-to-end exploit is not required. Reports need a
  minimal reproducer, the affected release or revision and environment, and the conditions that make
  the issue reachable.

---

## Reporting channels

| | **Public channel** | **Private channel** |
|---|---|---|
| **Use it for** | Ordinary bugs, and confirmed bugs not affecting any supported release target. | Any security issue affecting a supported release target, or any report whose security risk is uncertain. |
| **Where** | [Project-specific reporting links](SECURITY-specific.md#reporting-links). | [Project-specific reporting links](SECURITY-specific.md#reporting-links), or email [security@videolan.org](mailto:security@videolan.org). See [Submitting a report](#submitting-a-report) before submitting. |
| **Handling** | In the open, as an ordinary bug. No embargo. | Private during assessment. Invalid reports may be closed, ordinary bugs may move to the public channel, and confirmed security issues follow one of the two tracks under [Private-report handling](#private-report-handling). |

### Public vs private channel

Technical class alone does not determine the channel; the evidence and deployment context decide.
Use the private channel for any security issue affecting a supported release target, and whenever
the security risk is uncertain or the reporter is unsure.

"Supported release target" is defined in the
[project-specific security information](SECURITY-specific.md#scope-and-released-builds), which also
provides [non-exhaustive examples](SECURITY-specific.md#project-specific-examples) of issues for
each channel.

The reporter selects the initial channel. VideoLAN triage may close an invalid report, move an
ordinary bug to the public channel, or place a confirmed security issue on a handling track. Before
moving a report to the public channel, VideoLAN informs the reporter and allows removal of material
that should not be published.

### Private-report handling

Every report to the private channel remains private during assessment. Confirmed security issues
then use one of two tracks.

| Assessment | Workflow |
|---|---|
| **Low-risk security issue:** a confirmed security issue that does not satisfy both high-risk criteria. This includes unproven exploitability, impact that is not demonstrated to be significant, or both. | **Ordinary handling.** The fix is developed and published openly without a fix embargo. VideoLAN does not publish a security bulletin or request a CVE ID. |
| **High-risk security issue:** both exploitability and significant security impact are demonstrated. | **Coordinated disclosure.** The fix and vulnerability details remain private while releases are prepared. VideoLAN coordinates disclosure, publishes a security bulletin, and requests a CVE ID. |

In both tracks, the private report, discussion, and submitted evidence remain private until the
applicable dates in [Disclosure and timelines](#disclosure-and-timelines).

Exploitability and impact are assessed separately. Exploitability determines whether the security
consequence is proven. Impact is the consequence actually supported by that proof in the affected
project and deployment context. Significant security impact means meaningful harm to users,
systems, data, or a critical property identified by the project-specific policy.

Exploitability requires reproducible evidence on an affected release or supported use showing
attacker control and a technically supported path to a concrete security consequence.

---

## Submitting a report

For the public channel, use the project's public tracker. For the private channel, VideoLAN
strongly prefers the project's confidential tracker because it provides access-controlled,
centralized handling and lets
both VideoLAN and the reporter track the report and its status. Email
[security@videolan.org](mailto:security@videolan.org) remains available if the reporter prefers it,
is unsure which project is affected, or wants anonymous publication. VideoLAN routes valid email
reports and replies by email. Reports submitted to the wrong VideoLAN project, or involving several
VideoLAN projects, are routed internally.

Every report must include:

- a minimal reproducer: the smallest input or step sequence that triggers the bug;
- the affected release or revision and relevant environment;
- the conditions or path that make the issue reachable, including any required configuration or
  prerequisites.

Diagnostic evidence should be included when available. A clean reproducer and the information above
are enough for VideoLAN to reproduce and triage the report, but do not by themselves demonstrate
exploitability. VideoLAN determines the root cause.

---

## AI-assisted and automated reports

AI-assisted and automated reports are accepted when the reporter verifies a reproducible defect in
project code against a released version or current project revision. AI-hallucinated or otherwise
unvalidated findings and raw un-minimised fuzzer crashes with no minimal reproducer and no
reachability are not actionable and are not processed further. Long AI-written root-cause
narratives and speculative variant catalogues do not help triage. Repeated unvalidated submissions
may be rate-limited as a workload measure, not a judgement of the reporter.

---

## Response times

VideoLAN is run by volunteers, so there is no contractual SLA. VideoLAN aims to acknowledge private
reports within a few working days and provide an initial triage update through the channel used. If
no response arrives after several working days, the reporter may follow up through the same channel.

---

## Disclosure and timelines

Each project-specific policy identifies its supported release targets. A target's "fixing release"
is its first officially available release containing the fix. The 30-day publication clock starts
once every affected supported release target has a fixing release.

Ordinary handling has no fix embargo: the fix may be developed and published openly before every
affected supported release target has a fixing release. The report and submitted evidence
remain private until their publication date below.

Coordinated disclosure is reserved for high-risk security issues. The fix remains private while
release artifacts are prepared. Any coordination embargo begins with release prenotification and
lasts at most 14 days. For a qualifying non-public medium- or high-severity issue that is actionable
for multiple distributions, VideoLAN may notify the
[`distros` list](https://oss-security.openwall.org/wiki/mailing-lists/distros) once a fix is ready,
with public disclosure no more than 14 days later. At the agreed disclosure time, VideoLAN
simultaneously publishes the fix and security bulletin, makes fixing releases available for every
affected supported release target, and posts to
[`oss-security`](https://oss-security.openwall.org/wiki/mailing-lists/oss-security).

Public disclosure is permitted on this schedule:

- A public-channel issue: immediately. It is public from the start.
- A private-channel report handled ordinarily: 30 days after every affected supported release
  target has received a fixing release, when VideoLAN makes the report public. Its discussion,
  working exploit, reproducer, and crash corpus become public with it.
- A private-channel report under coordinated disclosure: at the agreed disclosure time, when the fix
  or vulnerability details become public, or 30 days after every affected supported release target
  has received a fixing release, whichever comes first. The report, discussion, working
  exploit, private reproducer, and crash corpus stay private until the 30-day date.

Security bulletins are published on the
[VideoLAN security page](https://www.videolan.org/security/), listing fixed and affected versions
and any CVE identifiers.

---

## CVEs

Before requesting a CVE ID or publishing a CVE Record for a VideoLAN project, the reporter should
contact [security@videolan.org](mailto:security@videolan.org). Sharing the proposed description,
affected versions, and security impact allows VideoLAN to verify the context and coordinate
disclosure before the record is published.

VideoLAN requests CVE IDs only for high-risk security issues handled through coordinated disclosure.
VideoLAN is not a CNA and uses
[MITRE's CNA of Last Resort](https://mitre.github.io/mitre-cve-roles/CNA-LR/).

---

## Credit

VideoLAN credits the reporter who submits the first actionable report meeting the
[submission requirements](#submitting-a-report), with that reporter's consent, and uses
anonymous attribution on request. For named credit, provide the name or handle to use. Credit
appears in the security bulletin for coordinated disclosure and in the release notes or equivalent
public change record otherwise.

---

## Safe harbour

VideoLAN will not pursue or support legal action against good-faith research conducted in
accordance with this policy, provided it avoids harm to VideoLAN infrastructure, users, and
third-party data and complies with applicable law.

This is a statement of intent, not a legal guarantee. Researchers uncertain whether an activity is
in scope should contact [security@videolan.org](mailto:security@videolan.org).
