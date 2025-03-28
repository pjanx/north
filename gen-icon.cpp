// gen-icon.cpp: generate program icons for north in SVG and PNG formats
//
// Copyright (c) 2025, Přemysl Eric Janouch <p@janouch.name>
// SPDX-License-Identifier: 0BSD

#include <QBuffer>
#include <QColor>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDomDocument>
#include <QFile>
#include <QImage>
#include <QMap>
#include <QPainter>
#include <QSvgRenderer>
#include <QTextStream>
#include <cmath>

#ifdef Q_OS_WIN32
#include <io.h>
#include <fcntl.h>
#endif

static const QColor colorTop(0x80, 0xc0, 0xff);
static const QColor colorBottom(0x58, 0x58, 0xc0);

static const QColor colorAccent = QColor::fromHsl(QColor(
	(colorTop.red()   + colorBottom.red())   / 2,
	(colorTop.green() + colorBottom.green()) / 2,
	(colorTop.blue()  + colorBottom.blue())  / 2).hue(), 0xff, 0xd0);

enum class IconKind { Generic, Small, Symbolic, Mac };

static const QMap<QString, IconKind> kindNames = {
	{"generic",  IconKind::Generic},
	{"small",    IconKind::Small},
	{"symbolic", IconKind::Symbolic},
	{"mac",      IconKind::Mac},
};

struct IconStyle {
	double scale, arrow_stroke_width;
	QString arrow_color, circle_color;
	double circle_opacity, circle_width;
};

static const QMap<IconKind, IconStyle> iconStyles = {
	{IconKind::Generic,  {16, .4, "#ffffff", colorAccent.name(), 1., .225}},
	{IconKind::Small,    {16, .3, "#ffffff", colorAccent.name(), 1., .3}},
	{IconKind::Symbolic, {18, .4, "#000000", "#000000", 1., .225}},
	{IconKind::Mac,      {12, .4, "#ffffff", "#ffffff", .625, .225}},
};

QDomElement appendElement(QDomNode &parent, QDomDocument &doc,
	const QString &tagName, const QMap<QString, QString> &attrs = {})
{
	QDomElement elem = doc.createElement(tagName);
	for (auto it = attrs.begin(); it != attrs.end(); ++it)
		elem.setAttribute(it.key(), it.value());
	parent.appendChild(elem);
	return elem;
}

// Apple uses something that's close to a "quintic superellipse" in their icons,
// but doesn't quite match. Either way, it looks better than rounded rectangles.
QString makeSquirclePath(double x, double y, double width, double height)
{
	auto superellipse = [](double value) {
		return std::copysign(std::pow(std::abs(value), 2. / 5.), value);
	};

	QString path = "M";
	double centerX = x + width / 2.;
	double centerY = y + height / 2.;
	// We won't use the SVG version of this at all, so it doesn't matter
	// if we draw it using way too many points.
	for (double theta = 0.0; theta < 2. * M_PI; theta += M_PI / 1e4) {
		double px = superellipse(std::cos(theta)) * width / 2. + centerX;
		double py = superellipse(std::sin(theta)) * height / 2. + centerY;
		path += QString(" %1,%2").arg(px).arg(py);
	}
	path += " Z";
	return path;
}

QDomDocument generateSVG(IconKind kind)
{
	const IconStyle &style = iconStyles[kind];
	static const QString arrowPath =
		"M 0,-1.3 +0.55,0.4 +0.2,0.25 0,1.3 -0.2,0.25 -0.55,0.4 z";

	QDomDocument doc;
	auto add = [&](QDomNode &parent, const QString &tagName,
		const QMap<QString, QString> &attrs = {}) {
		return appendElement(parent, doc, tagName, attrs);
	};
	auto svg = add(doc, "svg", {
		{"version", "1.1"}, {"xmlns", "http://www.w3.org/2000/svg"},
		{"width", "48"}, {"height", "48"}, {"viewBox", "0 0 48 48"},
	});

	// Create a mask to cut out the arrow and some space around it.
	auto defs = add(svg, "defs");
	auto mask = add(defs, "mask",
		{{"id", "arrow-cutout"}});
	add(mask, "rect", {
		{"fill", "#fff"},
		{"x", "-1.5"}, {"width", "3"},
		{"y", "-1.5"}, {"height", "3"},
	});
	add(mask, "path", {
		{"d", arrowPath},
		{"stroke", "#000"},
		{"stroke-width", QString::number(style.arrow_stroke_width)},
		{"stroke-linejoin", "round"},
	});

	// XXX: Maybe the normal icon should look more like the Mac icon,
	// (<circle cx="512" cy="512" r="475" ... /> and scale(15)),
	// at least to be used as the non-Mac program icon.
	if (kind == IconKind::Mac) {
		auto gradient = add(defs, "linearGradient", {
			{"id", "bg-gradient"},
			{"x1", "0%"}, {"x2", "0%"},
			{"y1", "0%"}, {"y2", "100%"},
		});
		// Qt SVG only supports RGB, not HSL or other stuff.
		add(gradient, "stop",
			{{"offset", "0%"}, {"stop-color", colorTop.name()}});
		add(gradient, "stop",
			{{"offset", "100%"}, {"stop-color", colorBottom.name()}});

		auto shadow = add(defs, "filter", {
			{"id", "shadow"},
			{"x", "-50%"}, {"width", "200%"},
			{"y", "-50%"}, {"height", "200%"},
			{"color-interpolation-filters", "sRGB"},
		});
		add(shadow, "feFlood",
			{{"flood-color", "#000"}, {"flood-opacity", "0.5"}});
		add(shadow, "feComposite",
			{{"in2", "SourceAlpha"}, {"operator", "in"}});
		add(shadow, "feOffset",
			{{"dy", "12"}});
		// It seems like Core Graphics shadow size
		// has a 2:1 relationship to standard deviation.
		add(shadow, "feGaussianBlur",
			{{"stdDeviation", "14"}});
		add(shadow, "feBlend",
			{{"in", "SourceGraphic"}, {"mode", "over"}});

		// macOS background put together.
		//
		// Qt SVG seems to lose or disregard feOffset decimal digits,
		// so we're actually forced to draw this shape on the 1024 scale.
		add(svg, "path", {
			{"d", makeSquirclePath(100, 100, 824, 824)},
			{"fill", "url(#bg-gradient)"},
			{"filter", "url(#shadow)"},
			{"transform", "scale(0.046875)"},
		});
	}

	QDomElement g = add(svg, "g", {
		{"transform", QString("translate(24 24) scale(%1)").arg(style.scale)},
	});

	// The generic and small styles include an outline around the symbolic icon.
	if (kind == IconKind::Generic || kind == IconKind::Small) {
		add(g, "circle",
			{{"cx", "0"}, {"cy", "0"}, {"r", "1.3"}});
		add(g, "path", {
			{"d", arrowPath},
			{"stroke", "#000"},
			{"stroke-width", QString::number(style.arrow_stroke_width)},
			{"stroke-linejoin", "round"},
		});
	}

	// A circle with ticks behind the arrow, and the arrow within.
	add(g, "path", {
		{"d", "M -1,0 h +0.5 M 1,0 h -0.5 "
			"M -1,0 A 1,1 0 1 1 +1,0 A 1,1 0 1 1 -1,0"},
		{"mask", "url(#arrow-cutout)"},
		{"fill", "none"},
		{"opacity", QString::number(style.circle_opacity)},
		{"stroke", style.circle_color},
		{"stroke-width", QString::number(style.circle_width)},
	});
	add(g, "path",
		{{"d", arrowPath}, {"fill", style.arrow_color}});

	// This is not OpenGL; drawing the arrow in two parts would look bad.
	if (kind == IconKind::Mac) {
		add(g, "path", {
			{"d", "M 0,-1.3 L +0.55,0.4 L +0.2,0.25 L 0,1.3 z"},
			{"fill", colorAccent.name()},
			{"opacity", "0.1"},
		});
	}
	return doc;
}

int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);
	app.setApplicationName(argv[0]);

	QCommandLineParser parser;
	parser.setApplicationDescription(
		"Generate an icon for north in SVG or PNG format");
	parser.addHelpOption();

	QCommandLineOption pngOption({"p", "png"},
		"Render PNG of specified size (otherwise output SVG)",
		"size");
	parser.addOption(pngOption);

	QCommandLineOption kindOption({"k", "kind"},
		"Icon kind: generic, small, symbolic, or mac (default: generic)",
		"kind", "generic");
	parser.addOption(kindOption);

	parser.process(app);

	QString kindStr = parser.value(kindOption);
	if (!parser.positionalArguments().isEmpty() ||
		!kindNames.contains(kindStr)) {
		parser.showMessageAndExit(QCommandLineParser::MessageType::Error,
			parser.helpText(), 2);
	}
	IconKind kind = kindNames[kindStr];

	// With QSvgGenerator we wouldn't be able to draw shadows,
	// so we generate an SVG XML, and render it through QSvgRenderer.
	//
	// Another option would be adding the shadow externally
	// as a post-processing step, which we would like to avoid.
	QDomDocument doc = generateSVG(kind);
	if (!parser.isSet(pngOption)) {
		QTextStream(stdout) << doc.toString(2);
		return 0;
	}

	bool ok;
	int size = parser.value(pngOption).toInt(&ok);
	if (!ok || size <= 0) {
		QTextStream(stderr)
			<< "Error: Invalid size: " << parser.value(pngOption)
			<< Qt::endl;
		return 2;
	}

	QSvgRenderer renderer(doc.toByteArray());
	if (!renderer.isValid()) {
		QTextStream(stderr) << "Error: Failed to load SVG" << Qt::endl;
		return 1;
	}

	QImage image(size, size, QImage::Format_ARGB32);
	image.fill(Qt::transparent);

	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing);
	renderer.render(&painter);
	painter.end();

#ifdef Q_OS_WIN32
	_setmode(fileno(stdout), _O_BINARY);
#endif

	QFile file;
	if (!file.open(stdout, QIODevice::WriteOnly)) {
		QTextStream(stderr)
			<< "Error: Failed to open stdout: " << file.errorString()
			<< Qt::endl;
		return 1;
	}
	if (!image.save(&file, "PNG") || !file.flush()) {
		QTextStream(stderr)
			<< "Error: Failed to write PNG: " << file.errorString()
			<< Qt::endl;
		return 1;
	}
	return 0;
}
