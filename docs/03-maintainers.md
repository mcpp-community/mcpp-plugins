# Maintainers

The repository is maintained mainly by
[@Sunrisepeak](https://github.com/Sunrisepeak); its upkeep and growth need
more contributors and maintainers.

## The path

Start from the simple and from what interests you, and learn a plugin as you
go:

**use it → introduce and teach it → report issues → fix bugs → improve it →
maintain one or more plugins**

## Which plugins this repository takes

This repository carries plugins that are general — a mechanism many projects
reuse — or that drive a widely used, well-known project. A build plugin
specific to one project is better kept beside that project: the plugin lives
in the project's repository, and the project publishes it to mcpp-index like
any library — a build plugin is an ordinary package.
[slint-m](https://github.com/mcpplibs/slint-m) is the example: the build rule
`mcpplibs.mplugin.slint` ships in Slint's own wrapper repository and is
published to the index, not here.

## What a maintainer does

Two core responsibilities, and both count:

| core | the work |
|---|---|
| Technical development | issue triage, fixes, the fixture and CI care on every platform the plugin claims |
| Ecosystem building and technical outreach | technical articles, videos, tutorials — and indirect ecosystem growth: real projects built on the plugin |

Development plus outreach is the core of the role; the second column is not
optional.

## What a maintainer knows

Deep familiarity with the plugin's module and its application domain. The
plugin's page in the [index](README.md) states what it does; the
[conventions](01-plugin-development.md) state what every plugin follows.

## Applying

Submissions follow the criterion above. Applications go through the
[issue template](https://github.com/mcpp-community/mcpp-plugins/issues/new?template=maintainer-application.yml),
one plugin per application. An application states contributions to the plugin
in both categories — technical development, and ecosystem building and
technical outreach — and a maintenance plan that covers both sides. A
recommendation from an existing maintainer of a related plugin supports it.
The original author of a plugin accepted into this repository becomes the
plugin's maintainer; bug fixes and improvements are always welcome before any
application.
