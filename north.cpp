// north.cpp: simple TOTP authenticator application
//
// Copyright (c) 2025, Přemysl Eric Janouch <p@janouch.name>
// SPDX-License-Identifier: 0BSD

#include <QCommandLineParser>
#include <QDateTime>
#include <QMessageAuthenticationCode>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamWriter>

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QGridLayout>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "north.h"

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#endif

static void setIsForeground([[maybe_unused]] bool isForeground)
{
#ifdef __APPLE__
	// Setting QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM should prevent
	// qt_mac_transformProccessToForegroundApplication from making us flash
	// in the dock as a foreground application, but in practice we need to
	// claim LSUIElement in Info.plist.

	ProcessSerialNumber psn = {0, kCurrentProcess};
	TransformProcessType(&psn, isForeground
		? kProcessTransformToForegroundApplication
		: kProcessTransformToUIElementApplication);
#endif
}

static QString escape_xml(const QString &input)
{
	QString output;
	QXmlStreamWriter writer(&output);
	writer.writeCharacters(input);
	return output;
}

static QString escape_utf8(const std::string &utf8)
{
	return escape_xml(QString::fromUtf8(utf8));
}

// -----------------------------------------------------------------------------

// RFC 4648/3548
static std::optional<std::vector<uint8_t>> base32_decode(const QString &base32)
{
	static const char lookup[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
	std::vector<uint8_t> result;
	unsigned buffer = 0, bits = 0;
	bool padding = false;
	for (QChar c : base32) {
		ptrdiff_t val = strchr(lookup, c.toLatin1()) - lookup;
		if (padding && c != '=')
			return {};

		if (c == '=')
			padding = true;
		else if (val < 0 || val >= 32)
			return {};

		buffer = (buffer << 5) | val;
		bits += 5;
		if (bits >= 8) {
			result.push_back(buffer >> (bits - 8));
			bits -= 8;
		}
	}
	return result;
}

// https://github.com/google/google-authenticator/wiki/Key-Uri-Format
// https://www.ietf.org/archive/id/draft-linuxgemini-otpauth-uri-02.html
// Parse a TOTP URI, returning any error condition.
QString TOTP::parse(const std::string &uri)
{
	QUrl url(QString::fromStdString(uri), QUrl::ParsingMode::StrictMode);
	if (!url.isValid())
		return "Invalid URI: " + url.errorString();
	if (url.scheme() != "otpauth")
		return "Invalid URI scheme";
	if (url.host() != "totp")
		return "Only TOTP is supported";

	auto path = url.path();
	if (path.startsWith("/"))
		path.removeFirst();

	if (auto colon = path.indexOf(':'); colon != -1) {
		this->issuer = path.mid(0, colon).toStdString();
		path.slice(colon + 1);
	}

	this->label = path.toStdString();

	QUrlQuery query(url.query());
	if (!query.hasQueryItem("secret"))
		return "Missing TOTP secret";

	auto secret = base32_decode(query.queryItemValue("secret"));
	if (!secret.has_value())
		return "Invalid TOTP secret";
	this->secret = *secret;

	if (query.hasQueryItem("algorithm")) {
		auto algorithm = query.queryItemValue("algorithm");
		if (algorithm == "SHA1")
			this->algorithm = TOTP::Algorithm::SHA1;
		else if (algorithm == "SHA256")
			this->algorithm = TOTP::Algorithm::SHA256;
		else if (algorithm == "SHA512")
			this->algorithm = TOTP::Algorithm::SHA512;
		else
			return "Unknown algorithm: " + algorithm;
	}
	if (query.hasQueryItem("digits")) {
		auto digits = query.queryItemValue("digits");
		if (digits.size() != 1 || digits[0] < '6' || digits[0] > '8')
			return "Invalid digits";
		this->digits = digits[0].digitValue();
	}
	if (query.hasQueryItem("period")) {
		bool ok = {};
		auto period = query.queryItemValue("period").toInt(&ok);
		if (!ok || period <= 0)
			return "Invalid period";
		this->period = period;
	}
	if (query.hasQueryItem("issuer"))
		this->issuer = query.queryItemValue("issuer").toStdString();
	return "";
}

QString TOTP::generate(quint64 timestamp) const
{
	quint64 counter = timestamp / this->period;

	QByteArray counterBytes(8, 0);
	for (int i = 7; i >= 0; --i) {
		counterBytes[i] = static_cast<char>(counter & 0xFF);
		counter >>= 8;
	}

	QCryptographicHash::Algorithm algo = QCryptographicHash::Sha1;
	switch (this->algorithm) {
	case Algorithm::SHA1:   algo = QCryptographicHash::Sha1;   break;
	case Algorithm::SHA256: algo = QCryptographicHash::Sha256; break;
	case Algorithm::SHA512: algo = QCryptographicHash::Sha512; break;
	}

	QByteArray hmac = QMessageAuthenticationCode::hash(
		counterBytes, QByteArray::fromRawData(
			(const char *)this->secret.data(), this->secret.size()), algo);

	int offset = hmac.at(hmac.size() - 1) & 0x0F;
	quint32 binary =
		(hmac[offset]     & 0x7F) << 24 |
		(hmac[offset + 1] & 0xFF) << 16 |
		(hmac[offset + 2] & 0xFF) <<  8 |
		(hmac[offset + 3] & 0xFF);

	auto code = binary % static_cast<quint32>(qPow(10, this->digits));
	return QString::number(code).rightJustified(this->digits, '0');
}

// -----------------------------------------------------------------------------

void TOTPItem::update()
{
	qint64 currentMs = QDateTime::currentMSecsSinceEpoch();
	qint64 currentTime = currentMs / 1000;
	qint64 periodMs = this->totp.period * 1000;
	qint64 msRemaining = periodMs - currentMs % periodMs;

	this->progress->setValue(
		msRemaining * this->progress->maximum() / periodMs);

	auto formattedCode = "<big>" + this->totp.generate(currentTime) + "</big>";
	if (this->code->text() != formattedCode)
		this->code->setText(formattedCode);
}

MainWindow::MainWindow(QWidget *parent, bool useTray)
	: QMainWindow(parent)
{
	this->setWindowTitle("north authenticator");

	loadSettings();

	updateTimer = new QTimer(this);
	connect(updateTimer, &QTimer::timeout, this, &MainWindow::updateTOTPs);

	systemTrayIcon = nullptr;
	if (useTray) {
		QIcon icon(":/systray.svg");
#ifdef Q_OS_MACOS
		// Masking is only really supported on Qt/macOS,
		// and we need a different icon for it.
		icon.setIsMask(true);
#endif
		systemTrayIcon = new QSystemTrayIcon(this);
		systemTrayIcon->setToolTip("north authenticator");
		systemTrayIcon->setIcon(icon);

		auto *menu = new QMenu(this);

		auto *widgetAction = new QWidgetAction(menu);
		totpWidget = buildTOTPWidget(menu);
		widgetAction->setDefaultWidget(totpWidget);
		menu->addAction(widgetAction);

		menu->addSeparator();

		auto *settingsAction = menu->addAction("&Settings...");
		connect(settingsAction, &QAction::triggered,
			this, &MainWindow::openSettings);
		auto *quitAction = menu->addAction("&Quit");
		connect(quitAction, &QAction::triggered,
			&QApplication::quit);

		systemTrayIcon->setContextMenu(menu);
#ifndef Q_OS_MACOS
		connect(systemTrayIcon, &QSystemTrayIcon::activated,
			this, [menu](QSystemTrayIcon::ActivationReason reason) {
				if (reason == QSystemTrayIcon::Trigger)
					menu->popup(QCursor::pos());
			});
#endif
		systemTrayIcon->show();
	} else {
		totpWidget = buildTOTPWidget(this);
		this->setCentralWidget(totpWidget);

		// Qt seems to automatically resolve menu bar differences on macOS.
		auto *menuBar = this->menuBar();
		auto *fileMenu = menuBar->addMenu("&File");

		auto *settingsAction = fileMenu->addAction("&Settings...");
		connect(settingsAction, &QAction::triggered,
			this, &MainWindow::openSettings);

		fileMenu->addSeparator();

		auto *quitAction = fileMenu->addAction("&Quit");
		connect(quitAction, &QAction::triggered,
			&QApplication::quit);
	}
}

QWidget *MainWindow::buildTOTPWidget(QWidget *parent)
{
	auto *widget = new QWidget(parent);
	auto *layout = new QGridLayout(widget);
	layout->setSpacing(5);
	layout->setContentsMargins(10, 10, 10, 10);

	if (this->uris.empty()) {
		auto *emptyLabel =
			new QLabel("There are no codes to generate.", widget);
		emptyLabel->setAlignment(Qt::AlignCenter);
		emptyLabel->setEnabled(false);
		layout->addWidget(emptyLabel, 0, 0);
		return widget;
	}

	int group = 0;
	for (const auto &uri : this->uris) {
		TOTP totp;
		auto err = totp.parse(uri);
		if (!err.isEmpty()) {
			QMessageBox::critical(this, "URI parse error", err);
			continue;
		}

		auto code = totp.generate(QDateTime::currentSecsSinceEpoch());

		auto *issuer_label =
			new QLabel("<b>" + escape_utf8(totp.issuer) + "</b>", widget);
		auto *name_label =
			new QLabel(escape_utf8(totp.label), widget);
		auto *code_label =
			new QLabel("<big>" + code + "</big>", widget);

		QIcon icon(":/edit-copy-symbolic.svg");
		icon.setIsMask(true);

		auto *copy_button = new QPushButton(widget);
		copy_button->setIcon(icon);
		copy_button->setToolTip("Copy to clipboard");

		auto *progress = new QProgressBar(widget);
		progress->setTextVisible(false);
		progress->setMaximum(totp.period * 10);

		auto *button_container = new QWidget(widget);
		auto *button_layout = new QVBoxLayout(button_container);
		button_layout->setContentsMargins(5, 0, 0, 0);
		button_layout->addWidget(copy_button);

		button_container->setSizePolicy(
			QSizePolicy::Fixed, QSizePolicy::Expanding);
		copy_button->setSizePolicy(
			QSizePolicy::Fixed, QSizePolicy::Expanding);

		layout->addWidget(issuer_label, group * 3, 0);
		layout->addWidget(name_label, group * 3, 1);
		layout->addWidget(button_container, group * 3, 2, 2, 1);
		layout->addWidget(code_label, group * 3 + 1, 0, 1, 2);
		layout->addWidget(progress, group * 3 + 2, 0, 1, 3);

		this->totps.emplace_back(std::move(totp), code_label, progress);

		size_t i = this->totps.size() - 1;
		connect(copy_button, &QPushButton::clicked, [this, i] {
			QApplication::clipboard()->setText(this->totps[i].totp.generate(
				QDateTime::currentSecsSinceEpoch()));
		});

		group++;
	}

	// Prevent vertical stretching by adding a spacer that consumes extra space.
	layout->setRowStretch(group * 3, 1);

	widget->installEventFilter(this);
	return widget;
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
	if (obj == totpWidget) {
		if (event->type() == QEvent::Show) {
			updateTimer->start(100);  // 10 FPS
			updateTOTPs();
		} else if (event->type() == QEvent::Hide) {
			updateTimer->stop();
		}
	}
	return QMainWindow::eventFilter(obj, event);
}

void MainWindow::openSettings()
{
	auto *dialog = new QDialog(this);
	dialog->setWindowTitle("Settings");

	auto *layout = new QVBoxLayout(dialog);

	auto *label = new QLabel("Enter otpauth:// URLs, one per line:", dialog);
	layout->addWidget(label);

	auto *textEdit = new QPlainTextEdit(dialog);
	textEdit->setPlainText(QString::fromStdString(
		std::accumulate(uris.begin(), uris.end(), std::string(),
			[](const std::string &a, const std::string &b) {
				return a.empty() ? b : a + "\n" + b;
			})));
	layout->addWidget(textEdit);

	auto *buttonBox = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
	connect(buttonBox, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
	connect(buttonBox, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
	layout->addWidget(buttonBox);

	dialog->resize(800, 400);
	dialog->move(dialog->screen()->geometry().center()
		- dialog->frameGeometry().center());

	// We mainly need to do this on macOS,
	// otherwise the window most likely will stay in the background.
	dialog->show();
	dialog->raise();

	if (systemTrayIcon)
		setIsForeground(true);

	auto result = dialog->exec();
	auto text = textEdit->toPlainText();
	delete dialog;

	if (systemTrayIcon)
		setIsForeground(false);

	if (result == QDialog::Accepted) {
		uris.clear();
		auto lines = text.split('\n');
		for (const auto &line : lines) {
			auto trimmed = line.trimmed();
			if (!trimmed.isEmpty())
				uris.push_back(trimmed.toStdString());
		}
		saveSettings();
		reloadTOTPs();
	}
}

void MainWindow::updateTOTPs()
{
	if (totpWidget)
		for (auto &item : this->totps)
			item.update();
}

void MainWindow::loadSettings()
{
	auto configPath = QStandardPaths::writableLocation(
		QStandardPaths::AppConfigLocation);
	auto filePath = QDir(configPath).filePath("north.json");

	QFile file(filePath);
	if (!file.exists())
		return;

	if (!file.open(QIODevice::ReadOnly)) {
		QMessageBox::warning(this, "Load error",
			"Could not open settings file: " + file.errorString());
		return;
	}

	QJsonParseError error;
	auto doc = QJsonDocument::fromJson(file.readAll(), &error);
	if (error.error != QJsonParseError::NoError) {
		QMessageBox::warning(this, "Parse error",
			"Could not parse settings file: " + error.errorString());
		return;
	}

	auto obj = doc.object();
	auto array = obj.value("urls").toArray();
	uris.clear();
	for (const auto &value : array) {
		if (value.isString())
			uris.push_back(value.toString().toStdString());
	}
}

void MainWindow::saveSettings()
{
	auto configPath = QStandardPaths::writableLocation(
		QStandardPaths::AppConfigLocation);

	QDir dir;
	if (!dir.mkpath(configPath)) {
		QMessageBox::warning(this, "Save error",
			"Could not create config directory");
		return;
	}

	QJsonArray array;
	for (const auto &uri : uris)
		array.append(QString::fromStdString(uri));

	QJsonObject obj;
	obj["version"] = 1;
	obj["urls"] = array;

	QJsonDocument doc(obj);
	auto filePath = QDir(configPath).filePath("north.json");
	QFile file(filePath);

	if (!file.open(QIODevice::WriteOnly)) {
		QMessageBox::warning(this, "Save error",
			"Could not open settings file for writing: " + file.errorString());
		return;
	}

	file.write(doc.toJson());
}

void MainWindow::reloadTOTPs()
{
	totps.clear();
	delete totpWidget;

	if (systemTrayIcon) {
		auto *menu = systemTrayIcon->contextMenu();
		if (auto *old = qobject_cast<QWidgetAction *>(menu->actions().first()))
			delete old;

		auto *widgetAction = new QWidgetAction(menu);
		totpWidget = buildTOTPWidget(menu);
		widgetAction->setDefaultWidget(totpWidget);
		menu->insertAction(menu->actions().first(), widgetAction);
	} else {
		totpWidget = buildTOTPWidget(this);
		this->setCentralWidget(totpWidget);
	}

	updateTOTPs();
}

// -----------------------------------------------------------------------------

int main(int argc, char *argv[])
{
	// This defaults to the program name, but nonetheless.
	QCoreApplication::setApplicationName("north");

	QApplication a(argc, argv);

	QCommandLineOption noTray{"no-tray", "Do not use the system tray"};
	QCommandLineParser parser;
	parser.setApplicationDescription("TOTP authenticator");
	parser.addHelpOption();
	parser.addVersionOption();
	parser.addOption(noTray);
	parser.process(a);

	bool useTray = QSystemTrayIcon::isSystemTrayAvailable() &&
		!parser.isSet(noTray);

	MainWindow w(nullptr, useTray);
	if (!useTray) {
		setIsForeground(true);
		w.show();
	}
	return a.exec();
}
