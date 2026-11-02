// north.h: simple TOTP authenticator application
//
// Copyright (c) 2025, Přemysl Eric Janouch <p@janouch.name>
// SPDX-License-Identifier: 0BSD

#pragma once

#include <QMainWindow>

class QLabel;
class QProgressBar;
class QSystemTrayIcon;
class QTimer;

struct TOTP
{
	// The issuer can be prepended to the label, and/or passed as a parameter.
	std::string issuer;
	std::string label;
	std::vector<uint8_t> secret;
	int period = 30;
	int digits = 6;
	enum class Algorithm { SHA1, SHA256, SHA512 } algorithm = Algorithm::SHA1;

	QString parse(const std::string &uri);
	QString generate(quint64 timestamp) const;
};

struct TOTPItem
{
	TOTP totp;
	QLabel *code;
	QProgressBar *progress;

	TOTPItem(TOTP totp, QLabel *code, QProgressBar *progress)
		: totp(totp), code(code), progress(progress) {}
	void update();
};

class MainWindow : public QMainWindow
{
	Q_OBJECT

	std::vector<std::string> uris;
	std::vector<TOTPItem> totps;
	QSystemTrayIcon *systemTrayIcon;
	QTimer *updateTimer;
	QWidget *totpWidget;

public:
	MainWindow(QWidget *parent, bool useTray);
	~MainWindow() {}

protected:
	void hideEvent(QHideEvent *event) override;
	void showEvent(QShowEvent *event) override;

private:
	QWidget *buildTOTPWidget(QWidget *parent);
	void loadSettings();
	void saveSettings();
	void startUpdating();
	void reloadTOTPs();

private slots:
	void openSettings();
	void updateTOTPs();
};
