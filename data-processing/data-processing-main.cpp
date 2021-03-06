/**
@mainpage Power Management Data Processing Main Window
@version 1.0
@author Ken Sarkies (www.jiggerjuice.net)
@date 22 March 2014

Utility program to aid in analysis if BMS data files.
*/

/****************************************************************************
 *   Copyright (C) 2013 by Ken Sarkies                                      *
 *   ksarkies@trinity.asn.au                                                *
 *                                                                          *
 *   This file is part of Power Management                                  *
 *                                                                          *
 *   Power Management is free software; you can redistribute it and/or      *
 *   modify it under the terms of the GNU General Public License as         *
 *   published by the Free Software Foundation; either version 2 of the     *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   Power Management is distributed in the hope that it will be useful,    *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU General Public License for more details.                           *
 *                                                                          *
 *   You should have received a copy of the GNU General Public License      *
 *   along with Power Management if not, write to the                       *
 *   Free Software Foundation, Inc.,                                        *
 *   51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.              *
 ***************************************************************************/


#include "data-processing-main.h"
#include <QApplication>
#include <QString>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QTextEdit>
#include <QCloseEvent>
#include <QFileDialog>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_grid.h>
#include <qwt_symbol.h>
#include <qwt_legend.h>
#include <qwt_date_scale_draw.h>
#include <qwt_date_scale_engine.h>
#include <cstdlib>
#include <iostream>
#include <unistd.h>

//-----------------------------------------------------------------------------
/** Power Management Data Processing Main Window Constructor

@param[in] parent Parent widget.
*/

DataProcessingGui::DataProcessingGui()
{
// Build the User Interface display from the Ui class in ui_mainwindowform.h
    DataProcessingMainUi.setupUi(this);
// Build the record type list
    recordType << "pH"  << "dT"  << "dD" << "ds";
    recordType << "dB1" << "dB2" << "dB3";
    recordType << "dC1" << "dC2" << "dC3";
    recordType << "dO1" << "dO2" << "dO3";
    recordType << "dL1" << "dL2" << "dM1";
    recordText << "Time" << "Temperature" << "Controls" << "Switch Setting";
    recordText << "Battery 1" << "Battery 2" << "Battery 3";
    recordText << "Charge State 1" << "Charge State 2" << "Charge State 3";
    recordText << "Charge Phase 1" << "Charge Phase 2" << "Charge Phase 3";
    recordText << "Load 1" << "Load 2" << "Panel";
    DataProcessingMainUi.recordType_1->addItem("None");
    DataProcessingMainUi.recordType_2->addItem("None");
    DataProcessingMainUi.recordType_3->addItem("None");
    DataProcessingMainUi.recordType_4->addItem("None");
    DataProcessingMainUi.recordType_5->addItem("None");
    for (int n=0; n<recordType.size(); n++)
    {
        DataProcessingMainUi.recordType_1->addItem(recordText[n]);
        DataProcessingMainUi.recordType_2->addItem(recordText[n]);
        DataProcessingMainUi.recordType_3->addItem(recordText[n]);
        DataProcessingMainUi.recordType_4->addItem(recordText[n]);
        DataProcessingMainUi.recordType_5->addItem(recordText[n]);
    }
    DataProcessingMainUi.intervalSpinBox->setMinimum(1);
    DataProcessingMainUi.intervalType->addItem("Average");
    DataProcessingMainUi.intervalType->addItem("Maximum");
    DataProcessingMainUi.intervalType->addItem("Sample");
// Build the energy table
    QStringList energyViewHeader;
    energyViewHeader << "Battery 1" << "Battery 2" << "Battery 3";
    energyViewHeader << "Load 1" << "Load 1" << "Panel" << "Total";
    DataProcessingMainUi.energyView->setHorizontalHeaderLabels(energyViewHeader);

    energyOutFile = NULL;
    outFile = NULL;
}

DataProcessingGui::~DataProcessingGui()
{
}

//-----------------------------------------------------------------------------
/** @brief Successful establishment of Window

@returns TRUE if successful.
*/
bool DataProcessingGui::success()
{
    return true;
}

//-----------------------------------------------------------------------------
/** @brief Open a raw data file for Reading.

This button only opens the file for reading.
*/

void DataProcessingGui::on_openReadFileButton_clicked()
{
    QString errorMessage;
    QFileInfo fileInfo;
    QString filename = QFileDialog::getOpenFileName(this,
                                "Data File","./","Text Files (*.txt *.TXT)");
    if (filename.isEmpty())
    {
        displayErrorMessage("No filename specified");
        return;
    }
    inFile = new QFile(filename);
    fileInfo.setFile(filename);
/* Look for start and end times, and determine current zero calibration */
    if (inFile->open(QIODevice::ReadOnly))
    {
        scanFile(inFile);
    }
    else
    {
        displayErrorMessage(QString("%1").arg((uint)inFile->error()));
    }
}

//-----------------------------------------------------------------------------
/** @brief Extract All to CSV.

The data files are expected to have the format of the BMS transmissions and
will be text files. 

All data is extracted to a csv file with one record per time interval. The
records subsequent to the time record are analysed and data is extracted
to the appropriate fields. The code expects the records to have a particular
order as sent by the BMS and the output has the same order without any
identification. The output format is suitable for spreadsheet analysis.
*/

void DataProcessingGui::on_dumpAllButton_clicked()
{
    QDateTime startTime = DataProcessingMainUi.startTime->dateTime();
    QDateTime endTime = DataProcessingMainUi.endTime->dateTime();
    if (! inFile->isOpen()) return;
    if (! openSaveFile()) return;
    inFile->seek(0);      // rewind input file
    combineRecords(startTime, endTime, inFile, outFile, true);
    if (saveFile.isEmpty())
        displayErrorMessage("File already closed");
    else
    {
        outFile->close();
        delete outFile;
//! Clear the name to prevent the same file being used.
        saveFile = QString();
    }
}

//-----------------------------------------------------------------------------
/** @brief Split raw or record files to day record files.

All data is extracted to a csv file with one record per time interval with
time starting at midnight and ending at midnight on the same day.

An input file must already be opened using the Open button. Start time is taken
from the first record of the input file and the end time is midnight.
The save file is created from the input file name and the date on the first
record of the input file. This is updated each time the record date changes.
If the save file exists, abort and create a parallel save file or append data.
*/

void DataProcessingGui::on_splitButton_clicked()
{
    if (! inFile->isOpen())
    {
        displayErrorMessage("Open the input file first");
        return;
    }
    QDateTime startTime = DataProcessingMainUi.startTime->dateTime();
    bool eof = false;
    bool header = true;
    while (! eof)
    {
        if (! startTime.isValid()) return;
// Set the end time to the record before midnight
        QDateTime endTime(startTime.date(),QTime(23,59,59));
// Create a save filename constructed from start date
        QString filename = QString("bms-data-")
                            .append(startTime.date().toString("yyyy.MM.dd"))
                            .append(".csv");
        QFileInfo fileInfo(filename);
        QDir saveDirectory = fileInfo.absolutePath();
        QString saveFile = saveDirectory.filePath(filename);
// If it exists, decide what action to take.
// Build a message box with options
        bool skip = false;
        if (QFile::exists(saveFile))
        {
            QMessageBox msgBox;
            msgBox.setText(QString("A previous save file ").append(filename).
                            append(" exists."));
// Overwrite the existing file
            QPushButton *overwriteButton = msgBox.addButton(tr("Overwrite"),
                             QMessageBox::AcceptRole);
// Append to the existing file
            QPushButton *appendButton = msgBox.addButton(tr("Append"),
                             QMessageBox::AcceptRole);
// Make a different filename by adding a character at the end
            QPushButton *parallelButton = msgBox.addButton(tr("New File"),
                             QMessageBox::AcceptRole);
// Skip this file and go on to the next
            QPushButton *skipButton = msgBox.addButton(tr("Skip"),
                            QMessageBox::AcceptRole);
// Quit altogether
            QPushButton *abortButton = msgBox.addButton(tr("Abort"),
                             QMessageBox::AcceptRole);
            msgBox.exec();
            if (msgBox.clickedButton() == overwriteButton)
            {
                QFile::remove(saveFile);
            }
            else if (msgBox.clickedButton() == parallelButton)
            {
                filename = filename.left(filename.length()-4).append("-a.csv");
                QFileInfo fileInfo(filename);
                QDir saveDirectory = fileInfo.absolutePath();
                saveFile = saveDirectory.filePath(filename);
            }
            else if (msgBox.clickedButton() == skipButton)
            {
                skip = true;
            }
            else if (msgBox.clickedButton() == abortButton)
            {
                return;
            }
// Don't write the header into the appended file
            else if (msgBox.clickedButton() == appendButton)
            {
                header = false;
            }
        }
// This will write to the file as created above, or append to the existing file.
        if (! skip)
        {
            QFile* outFile = new QFile(saveFile);   // Open file for output
            if (! outFile->open(QIODevice::WriteOnly | QIODevice::Append
                                                     | QIODevice::Text))
            {
                displayErrorMessage("Could not open the output file");
                return;
            }
            inFile->seek(0);      // rewind input file
            eof = combineRecords(startTime, endTime, inFile, outFile, header);
            header = true;
            outFile->close();
            delete outFile;
        }
        startTime = QDateTime(startTime.date().addDays(1),QTime(0,0,0));
        if (startTime > DataProcessingMainUi.endTime->dateTime()) return;
    }
}

//-----------------------------------------------------------------------------
/** @brief Find Energy Balance.

This is taken from the RAW file.

Add up the ampere hour energy taken from batteries and supplied by the source
over the specified time interval. Display these in a table form. The battery
energy includes loads and sources and therefore itself provides sufficient
information. The loads and sources alone do not account for onboard electronics
power usage. The total balance is displayed.

The load and source currents show large negative swings when the undervoltage
or overcurrent indicators are triggered. Any negative swing on those currents
is set to zero. The indicator settings are captured but not used at this stage.
*/

void DataProcessingGui::on_energyButton_clicked()
{
    if (inFile == NULL) return;
    if (! inFile->isOpen()) return;
    inFile->seek(0);      // rewind file
    tableRow = 0;
//    int interval = DataProcessingMainUi.intervalSpinBox->value();
//    int intervaltype = DataProcessingMainUi.intervalType->currentIndex();
    QTextStream inStream(inFile);
    QDateTime startTime = DataProcessingMainUi.startTime->dateTime();
    QDateTime finalTime = DataProcessingMainUi.endTime->dateTime();
    QDateTime time = startTime;
    QDateTime previousTime = startTime;
// Cumulative energy measures
    long long battery1Energy = 0;
    long long battery2Energy = 0;
    long long battery3Energy = 0;
    long long load1Energy = 0;
    long long load2Energy = 0;
    long long panelEnergy = 0;
    long battery1Seconds = 0;
    long battery2Seconds = 0;
    long battery3Seconds = 0;
    long load1Seconds = 0;
    long load2Seconds = 0;
    long panelSeconds = 0;
    long elapsedSeconds = 0;
//    int indicators = 0;
    DataProcessingMainUi.energyView->clear();
// Set the end time to the record before midnight
    QDateTime endTime(startTime.date(),QTime(23,59,59));
    while (true)
    {
        if (! inStream.atEnd())
        {
          	QString lineIn = inStream.readLine();
            QStringList breakdown = lineIn.split(",");
            int size = breakdown.size();
            if (size <= 0) break;
            QString firstText = breakdown[0].simplified();
            int secondField = 0;
//            int thirdField = 0;
            if (size > 1)
            {
// Extract the time record for time range comparison.
// records are nominally 0.5 seconds apart but QT doesn't have fractions of
// a second. Therefore some intervals will be zero. This method however accounts
// for gaps in the records.
                if (firstText == "pH")
                {
                    previousTime = time;
                    time = QDateTime::fromString(breakdown[1].simplified(),Qt::ISODate);
                    elapsedSeconds = previousTime.secsTo(time);
                }
                else
                {
                    secondField = breakdown[1].simplified().toInt();
                }
            }
//            if (size > 2) thirdField = breakdown[2].simplified().toInt();
// Extract records of measured currents and add up. The second field is the
// current times 256 and the third is the voltage times 256 (not needed).
            if (time >= startTime)
            {
                if (firstText == "dB1")
                {
                    int battery1Current = secondField-battery1CurrentZero;
                    battery1Energy += battery1Current*elapsedSeconds;
                    battery1Seconds += elapsedSeconds;
                }
                if (firstText == "dB2")
                {
                    int battery2Current = secondField-battery2CurrentZero;
                    battery2Energy += battery2Current*elapsedSeconds;
                    battery2Seconds += elapsedSeconds;
                }
                if (firstText == "dB3")
                {
                    int battery3Current = secondField-battery3CurrentZero;
                    battery3Energy += battery3Current*elapsedSeconds;
                    battery3Seconds += elapsedSeconds;
                }
// Sum only positive currents. Negatives are phantoms due to electronics.
                if (firstText == "dL1")
                {
                    int load1Current = secondField;
                    if (load1Current < 0) load1Current = 0;
                    load1Energy += load1Current*elapsedSeconds;
                    load1Seconds += elapsedSeconds;
                }
                if (firstText == "dL2")
                {
                    int load2Current = secondField;
                    if (load2Current < 0) load2Current = 0;
                    load2Energy += load2Current*elapsedSeconds;
                    load2Seconds += elapsedSeconds;
                }
                if (firstText == "dM1")
                {
                    int panelCurrent = secondField;
                    if (panelCurrent < 0) panelCurrent = 0;
                    panelEnergy += panelCurrent*elapsedSeconds;
                    panelSeconds += elapsedSeconds;
                }
// Get record of indicators (not needed)
//                if (firstText == "dI") indicators = secondField;
            }
        }
// Completion of a day or file. Print out and get ready for next.
        if  ((time > endTime) || inStream.atEnd())
        {
// Add a row if necessary
            if (tableRow >= DataProcessingMainUi.energyView->rowCount())
                DataProcessingMainUi.energyView->setRowCount(tableRow+1);

            QDate date = startTime.date();
            QTableWidgetItem *day = new QTableWidgetItem(date.toString("dd/MM/yy"));
            DataProcessingMainUi.energyView->setItem(tableRow, 0, day);
            QTableWidgetItem *battery1Item = new QTableWidgetItem(tr("%1")
                 .arg((float)battery1Energy/921600,0,'g',3));
            DataProcessingMainUi.energyView->setItem(tableRow, 1, battery1Item);
            QTableWidgetItem *battery2Item = new QTableWidgetItem(tr("%1")
                 .arg((float)battery2Energy/921600,0,'g',3));
            DataProcessingMainUi.energyView->setItem(tableRow, 2, battery2Item);
            QTableWidgetItem *battery3Item = new QTableWidgetItem(tr("%1")
                 .arg((float)battery3Energy/921600,0,'g',3));
            DataProcessingMainUi.energyView->setItem(tableRow, 3, battery3Item);
            QTableWidgetItem *load1Item = new QTableWidgetItem(tr("%1")
                 .arg((float)load1Energy/921600,0,'g',3));
            DataProcessingMainUi.energyView->setItem(tableRow, 4, load1Item);
            QTableWidgetItem *load2Item = new QTableWidgetItem(tr("%1")
                 .arg((float)load2Energy/921600,0,'g',3));
            DataProcessingMainUi.energyView->setItem(tableRow, 5, load2Item);
            QTableWidgetItem *panelItem = new QTableWidgetItem(tr("%1")
                 .arg((float)panelEnergy/921600,0,'g',3));
            DataProcessingMainUi.energyView->setItem(tableRow, 6, panelItem);
// Display total energy used (negative if charging) in last column
            long long totalEnergy = battery1Energy+battery2Energy+battery3Energy;
            QTableWidgetItem *energyTotal = new QTableWidgetItem(tr("%1")
                 .arg((float)totalEnergy/921600,0,'g',3));
            QFont tableFont = QApplication::font();
            tableFont.setBold(true);
            energyTotal->setFont(tableFont);
            DataProcessingMainUi.energyView->setItem(tableRow, 7, energyTotal);

            if (inStream.atEnd()) break;

// Reset energy measures
            battery1Energy = 0;
            battery2Energy = 0;
            battery3Energy = 0;
            load1Energy = 0;
            load2Energy = 0;
            panelEnergy = 0;
            battery1Seconds = 0;
            battery2Seconds = 0;
            battery3Seconds = 0;
            load1Seconds = 0;
            load2Seconds = 0;
            panelSeconds = 0;
            elapsedSeconds = 0;

            tableRow++;
// New start and end times
            startTime = QDateTime(startTime.date().addDays(1),QTime(0,0,0));
            if (startTime > DataProcessingMainUi.endTime->dateTime()) return;
            endTime = QDateTime(startTime.date(),QTime(23,59,59));
            if (endTime > DataProcessingMainUi.endTime->dateTime())
                endTime = DataProcessingMainUi.endTime->dateTime();
        }
    }
}

//-----------------------------------------------------------------------------
/** @brief Save Energy Computations.

*/

void DataProcessingGui::on_energySaveButton_clicked()
{
    QString filename = QFileDialog::getSaveFileName(this,
                        "Acquisition Save Acquired Data",
                        QString(),
                        "Comma Separated Variables (*.csv *.txt)");
    if (filename.isEmpty()) return;
    if (! filename.endsWith(".csv")) filename.append(".csv");
    QFileInfo fileInfo(filename);
    saveDirectory = fileInfo.absolutePath();
    energySaveFile = saveDirectory.filePath(filename);
    energyOutFile = new QFile(energySaveFile);          // Open file for output
    if (! energyOutFile->open(QIODevice::WriteOnly))
    {
        displayErrorMessage("Could not open the output file");
        return;
    }
    QTextStream outStream(energyOutFile);
    int numberColumns = DataProcessingMainUi.energyView->columnCount();
    for (int row = 0; row<tableRow; row++)
    {
        for (int column = 0; column<numberColumns; column++)
        {
            if (column > 0) outStream << ",";
            QString item = DataProcessingMainUi.energyView->item(row,column)->text();
            outStream << item;
        }
        outStream << "\n\r";
    }
    energyOutFile->close();
    delete energyOutFile;
}

//-----------------------------------------------------------------------------
/** @brief Extract Data.

Up to three data sets specified are extracted and written to a file.

An interval is specified over which data may be taken as the first sample, the
maximum or the average. The time over which the extraction occurs can be
specified.

A header is built from the selected record types specified.
Each record when found is written directly, excluding the ident field.
This means that some records will have more than one field and so are dealt
with individually.
*/

void DataProcessingGui::on_extractButton_clicked()
{
    if (inFile == NULL) return;
    if (! inFile->isOpen()) return;
    if (! openSaveFile()) return;
    inFile->seek(0);      // rewind input file
//    int interval = DataProcessingMainUi.intervalSpinBox->value();
//    int intervaltype = DataProcessingMainUi.intervalType->currentIndex();
    QTextStream inStream(inFile);
    QTextStream outStream(outFile);
    QDateTime startTime = DataProcessingMainUi.startTime->dateTime();
    QDateTime endTime = DataProcessingMainUi.endTime->dateTime();
    QDateTime time;
    QString header;
    QString comboRecord;
    int recordType_1 = DataProcessingMainUi.recordType_1->currentIndex();
    int recordType_2 = DataProcessingMainUi.recordType_2->currentIndex();
    int recordType_3 = DataProcessingMainUi.recordType_3->currentIndex();
    int recordType_4 = DataProcessingMainUi.recordType_4->currentIndex();
    int recordType_5 = DataProcessingMainUi.recordType_5->currentIndex();
// The first time record is a reference. Anything before that must be ignored.
// firstTime allows the program to build a header and the first record.
    bool firstTime = true;
// The first record only is preceded by the constructed header.
    bool firstRecord = true;
    while (! inStream.atEnd())
    {
      	QString lineIn = inStream.readLine();
        QStringList breakdown = lineIn.split(",");
        int size = breakdown.size();
        if (size <= 0) break;
        QString firstText = breakdown[0].simplified();
// Extract the time record for time range comparison.
        if (size > 1)
        {
            if (firstText == "pH")
            {
                time = QDateTime::fromString(breakdown[1].simplified(),Qt::ISODate);
                if ((time >= startTime) && (time <= endTime))
                {
                    if (!firstTime)
                    {
// On the first pass output the accumulated header string.
                        if (firstRecord)
                        {
                            outStream << header << "\n\r";
                            firstRecord = false;
                        }
// Output the combined record and null it for next pass.
                        outStream << comboRecord << "\n\r";
                        comboRecord = QString();
                    }
                    firstTime = false;
                }
            }
        }
// Extract records after the reference time record and between specified times.
        if (!firstTime && (time >= startTime) && (time <= endTime))
        {
// Find the relevant records and extract their fields.
            int rec = 0;
            if ((recordType_1 > 0) && (firstText == recordType[recordType_1-1]))
                rec = recordType_1;
            if ((recordType_2 > 0) && (firstText == recordType[recordType_2-1]))
                rec = recordType_2;
            if ((recordType_3 > 0) && (firstText == recordType[recordType_3-1]))
                rec = recordType_3;
            if ((recordType_4 > 0) && (firstText == recordType[recordType_4-1]))
                rec = recordType_4;
            if ((recordType_5 > 0) && (firstText == recordType[recordType_5-1]))
                rec = recordType_5;
            if (rec > 0)
            {
                if (firstRecord)
                {
                    if (! header.isEmpty()) header += ",";
                    header += recordText[rec-1];
                    if (size > 2) header += " I," + recordText[rec-1] + " V";
                }
                if (! comboRecord.isEmpty()) comboRecord += ",";
                comboRecord += breakdown[1].simplified();
                if (size > 2) comboRecord += "," + breakdown[2].simplified();
            }
        }
    }
    if (saveFile.isEmpty())
        displayErrorMessage("File already closed");
    else
    {
        outFile->close();
        delete outFile;
//! Null the name to prevent the same file being used.
        saveFile = QString();
    }
}

//-----------------------------------------------------------------------------
/** @brief Select Voltages to be plotted
*/

void DataProcessingGui::on_voltagePlotCheckBox_clicked()
{
    if (DataProcessingMainUi.voltagePlotCheckBox->isChecked())
    {
        DataProcessingMainUi.moduleCheckbox->setEnabled(false);
        DataProcessingMainUi.moduleCheckbox->setVisible(false);
    }
    else
    {
        DataProcessingMainUi.moduleCheckbox->setEnabled(true);
        DataProcessingMainUi.moduleCheckbox->setVisible(true);
    }
    DataProcessingMainUi.statesPlotCheckbox->setChecked(false);
    DataProcessingMainUi.temperaturePlotCheckbox->setChecked(false);
}

//-----------------------------------------------------------------------------
/** @brief Action taken on battery1 checkbox selected
*/

void DataProcessingGui::on_battery1Checkbox_clicked()
{
    if (DataProcessingMainUi.statesPlotCheckbox->isChecked())
    {
        DataProcessingMainUi.battery2Checkbox->setChecked(false);
        DataProcessingMainUi.battery3Checkbox->setChecked(false);
    }
}

//-----------------------------------------------------------------------------
/** @brief Action taken on battery2 checkbox selected
*/

void DataProcessingGui::on_battery2Checkbox_clicked()
{
    if (DataProcessingMainUi.statesPlotCheckbox->isChecked())
    {
        DataProcessingMainUi.battery1Checkbox->setChecked(false);
        DataProcessingMainUi.battery3Checkbox->setChecked(false);
    }
}

//-----------------------------------------------------------------------------
/** @brief Action taken on battery3 checkbox selected
*/

void DataProcessingGui::on_battery3Checkbox_clicked()
{
    if (DataProcessingMainUi.statesPlotCheckbox->isChecked())
    {
        DataProcessingMainUi.battery2Checkbox->setChecked(false);
        DataProcessingMainUi.battery1Checkbox->setChecked(false);
    }
}

//-----------------------------------------------------------------------------
/** @brief Action taken on states checkbox selected
*/

void DataProcessingGui::on_statesPlotCheckbox_clicked()
{
// Only one battery can be selected at a time
    if (DataProcessingMainUi.statesPlotCheckbox->isChecked())
    {
        if (DataProcessingMainUi.battery1Checkbox->isChecked())
        {
            DataProcessingMainUi.battery2Checkbox->setChecked(false);
            DataProcessingMainUi.battery3Checkbox->setChecked(false);
        }
        else if (DataProcessingMainUi.battery2Checkbox->isChecked())
        {
            DataProcessingMainUi.battery3Checkbox->setChecked(false);
        }
        DataProcessingMainUi.moduleCheckbox->setChecked(false);
        DataProcessingMainUi.voltagePlotCheckBox->setChecked(false);
        DataProcessingMainUi.temperaturePlotCheckbox->setChecked(false);
    }
}

//-----------------------------------------------------------------------------
/** @brief Action taken on temperature checkbox selected
*/

void DataProcessingGui::on_temperaturePlotCheckbox_clicked()
{
    if (DataProcessingMainUi.temperaturePlotCheckbox->isChecked())
    {
        DataProcessingMainUi.battery1Checkbox->setChecked(false);
        DataProcessingMainUi.battery2Checkbox->setChecked(false);
        DataProcessingMainUi.battery3Checkbox->setChecked(false);
        DataProcessingMainUi.moduleCheckbox->setChecked(false);
        DataProcessingMainUi.voltagePlotCheckBox->setChecked(false);
        DataProcessingMainUi.statesPlotCheckbox->setChecked(false);
    }
}

//-----------------------------------------------------------------------------
/** @brief Select File to be plotted and execute the plot

@todo This procedure deals with all valid plots and as such is a bit involved.
Later split out into a separate window with different procedures and more
options.
*/

void DataProcessingGui::on_plotFileSelectButton_clicked()
{
    bool showCurrent = ! DataProcessingMainUi.voltagePlotCheckBox->isChecked();
    bool showTemperature = DataProcessingMainUi.temperaturePlotCheckbox->isChecked();
    bool showStates = DataProcessingMainUi.statesPlotCheckbox->isChecked();
    float yScaleLow,yScaleHigh;
    bool showPlot1,showPlot2,showPlot3,showPlot4;
    int i1 = 0;
    int i2 = 0;
    int i3 = 0;
    int i4 = 0;                // Columns for data series

// Get data file
    QString fileName = QFileDialog::getOpenFileName(0,
                                "Data File","./","CSV Files (*.csv)");
    if (fileName.isEmpty()) return;
    QFileInfo fileInfo;
    QFile* inFile = new QFile(fileName);
    fileInfo.setFile(fileName);
    if (! inFile->open(QIODevice::ReadOnly)) return;
    QTextStream inStream(inFile);

// Setup Plot objects
    QwtPlotCurve *curve1;
    curve1 = new QwtPlotCurve();
    QPolygonF points1;
    QwtPlotCurve *curve2;
    curve2 = new QwtPlotCurve();
    QPolygonF points2;
    QwtPlotCurve *curve3;
    curve3 = new QwtPlotCurve();
    QPolygonF points3;
    QwtPlotCurve *curve4;
    curve4 = new QwtPlotCurve();
    QPolygonF points4;

// States display needs massaging of the data
    if (showStates)                 // SoC, Voltage and charge state
    {
        showPlot1 = true;
        showPlot2 = true;
        showPlot3 = true;
        showPlot4 = false;
        if (DataProcessingMainUi.battery1Checkbox->isChecked())
        {
            i1 = 2;
            i2 = 3;
            i3 = 4;
        }
        else if (DataProcessingMainUi.battery2Checkbox->isChecked())
        {
            i1 = 8;
            i2 = 9;
            i3 = 10;
        }
        else if (DataProcessingMainUi.battery3Checkbox->isChecked())
        {
            i1 = 14;
            i2 = 15;
            i3 = 16;
        }
        yScaleLow = 0;
        yScaleHigh = 100;
    }
// At present Temperature ticked shows only the one plot.
    else if (showTemperature)       // Temperature
    {
        showPlot1 = true;
        showPlot2 = false;
        showPlot3 = false;
        showPlot4 = false;
        i1 = 25;
        yScaleLow = -10;
        yScaleHigh = 50;
    }
    else if (showCurrent)           // Current
    {
        showPlot1 = DataProcessingMainUi.battery1Checkbox->isChecked();
        showPlot2 = DataProcessingMainUi.battery2Checkbox->isChecked();
        showPlot3 = DataProcessingMainUi.battery3Checkbox->isChecked();
        showPlot4 = DataProcessingMainUi.moduleCheckbox->isChecked();
        i1 = 1;
        i2 = 7;
        i3 = 13;
        i4 = 23;
        yScaleLow = -20;
        yScaleHigh = 20;
    }
    else                            // Voltage
    {
        showPlot1 = DataProcessingMainUi.battery1Checkbox->isChecked();
        showPlot2 = DataProcessingMainUi.battery2Checkbox->isChecked();
        showPlot3 = DataProcessingMainUi.battery3Checkbox->isChecked();
        showPlot4 = false;
        i1 = 2;
        i2 = 8;
        i3 = 14;
        yScaleLow = 10;
        yScaleHigh = 18;
    }

// Set display parameters and titles
    if (showStates)                 // SoC, Voltage and charge state
    {
        curve1->setTitle("Voltage");
        curve1->setPen(Qt::blue, 2),
        curve1->setRenderHint(QwtPlotItem::RenderAntialiased, true);
        curve2->setTitle("State of Charge");
        curve2->setPen(Qt::red, 2),
        curve2->setRenderHint(QwtPlotItem::RenderAntialiased, true);
        curve3->setTitle("Charging Mode");
        curve3->setPen(Qt::black, 2),
        curve3->setRenderHint(QwtPlotItem::RenderAntialiased, true);
    }
    else
    {
        if (showPlot1)
        {
            if (showTemperature) curve1->setTitle("Temperature");
            else curve1->setTitle("Battery 1");
            curve1->setPen(Qt::blue, 2),
            curve1->setRenderHint(QwtPlotItem::RenderAntialiased, true);
        }
        if (showPlot2)
        {
            curve2->setTitle("Battery 2");
            curve2->setPen(Qt::red, 2),
            curve2->setRenderHint(QwtPlotItem::RenderAntialiased, true);
        }
        if (showPlot3)
        {
            curve3->setTitle("Battery 3");
            curve3->setPen(Qt::yellow, 2),
            curve3->setRenderHint(QwtPlotItem::RenderAntialiased, true);
        }
        if (showPlot4)
        {
            curve4->setTitle("Module");
            curve4->setPen(Qt::green, 2),
            curve4->setRenderHint(QwtPlotItem::RenderAntialiased, true);
        }
    }

    bool ok;
// Read in data from input file
// Skip first line as it may be a header
  	QString lineIn;
    lineIn = inStream.readLine();
    bool startRun = true;
// Index increments by about 0.5 seconds
// To have x-axis in date-time index must be "double" type, ie ms since epoch.
    double index = 0;
    QDateTime startTime;
    QDateTime previousTime;
    while (! inStream.atEnd())
    {
      	lineIn = inStream.readLine();
        QStringList breakdown = lineIn.split(",");
        int size = breakdown.size();
        if (size == LINE_WIDTH)
        {
            QDateTime time = QDateTime::fromString(breakdown[0].simplified(),Qt::ISODate);
            if (time.isValid())
            {
// On the first run get the start time
                if (startRun)
                {
                    startRun = false;
                    startTime = time;
                    previousTime = startTime;
                    index = startTime.toMSecsSinceEpoch();
                }
// Try to keep index and time in sync to account for jumps in time.
// Index is counting half seconds and time from records is integer seconds only
                if (previousTime == time) index += 500;
                else index = time.toMSecsSinceEpoch();
// Create points to plot
                if (showStates)
                {
// In this case data to be displayed needs to be converted to common scale.
                    float batteryVoltage = (breakdown[i1].simplified().toFloat(&ok)-10)*100/10;
                    points1 << QPointF(index,batteryVoltage);
                    float stateOfCharge = breakdown[i2].simplified().toFloat(&ok);
                    points2 << QPointF(index,stateOfCharge);
                    QString chargeModetext = breakdown[i3].simplified();
                    float chargeMode = 0;
                    if (chargeModetext == "Isolate") chargeMode = 5;
                    if (chargeModetext == "Charge") chargeMode = 10;
                    if (chargeModetext == "Loaded") chargeMode = 0;
                    points3 << QPointF(index,chargeMode);
                }
                else
                {
                    if (showPlot1)
                    {
                        float currentBattery1 = breakdown[i1].simplified().toFloat(&ok);
                        points1 << QPointF(index,currentBattery1);
                    }
                    if (showPlot2)
                    {
                        float currentBattery2 = breakdown[i2].simplified().toFloat(&ok);
                        points2 << QPointF(index,currentBattery2);
                    }
                    if (showPlot3)
                    {
                        float currentBattery3 = breakdown[i3].simplified().toFloat(&ok);
                        points3 << QPointF(index,currentBattery3);
                    }
                    if (showPlot4)
                    {
                        float currentModule = breakdown[i4].simplified().toFloat(&ok);
                        points4 << QPointF(index,currentModule);
                    }
                }
            }
        }
    }
// Build plot
    QwtPlot *plot = new QwtPlot(0);
    if (showStates) plot->setTitle("Battery States");
    else if (showTemperature) plot->setTitle("Battery Temperature");
    else
    {
        if (showCurrent) plot->setTitle("Battery Currents");
        else plot->setTitle("Battery Voltages");
    }
    plot->setCanvasBackground(Qt::white);
    plot->setAxisScale(QwtPlot::yLeft, yScaleLow, yScaleHigh);
    //Set x-axis scaling.
    QwtDateScaleDraw *qwtDateScaleDraw = new QwtDateScaleDraw(Qt::LocalTime);
    QwtDateScaleEngine *qwtDateScaleEngine = new QwtDateScaleEngine(Qt::LocalTime);
    qwtDateScaleDraw->setDateFormat(QwtDate::Hour, "hh");
 
    plot->setAxisScaleDraw(QwtPlot::xBottom, qwtDateScaleDraw);
    plot->setAxisScaleEngine(QwtPlot::xBottom, qwtDateScaleEngine);
//    plot->setAxisScale(QwtPlot::xBottom, xScaleLow, xScaleHigh);
    plot->insertLegend(new QwtLegend());
    QwtPlotGrid *grid = new QwtPlotGrid();
    grid->attach(plot);

    if (showPlot1)
    {
        curve1->setSamples(points1);
        curve1->attach(plot);
    }
    if (showPlot2)
    {
        curve2->setSamples(points2);
        curve2->attach(plot);
    }
    if (showPlot3)
    {
        curve3->setSamples(points3);
        curve3->attach(plot);
    }
    if (showPlot4)
    {
        curve4->setSamples(points4);
        curve4->attach(plot);
    }

    plot->resize(1000,600);
    plot->show();
}

//-----------------------------------------------------------------------------
/** @brief Analysis of CSV files for various performance indicators.

The file is analysed for a variety of faults and other performance indicators.

The results are printed out to a report file.

- Situations where the charger is not allocated but a battery is ready. To show
  this look for no battery under charge and panel voltage above any battery.
  Print the op state, charging phase, battery and panel voltages, switches,
  decision and indicators.

- Battery current during the charging phase to show in particular how the float
  state is reached and if this is a true indication of a battery being fully
  charged.

- Solar current input derived from all batteries as they are charged in the bulk
  phase. This may cut short during any one day if all batteries enter the float
  state and the charger is de-allocated.
*/

void DataProcessingGui::on_analysisFileSelectButton_clicked()
{
// Get the input data file
    QString inputFilename = QFileDialog::getOpenFileName(0,
                                "Data File","./","CSV Files (*.csv)");
    if (inputFilename.isEmpty()) return;
    QFile* inFile = new QFile(inputFilename);
    fileInfo.setFile(inputFilename);
    if (! inFile->open(QIODevice::ReadOnly)) return;
    QTextStream inStream(inFile);

// Create a unique output report filename from the input filename and date-time
    QFileInfo inputFileInfo(inFile->fileName());
    QString inputFileStub(inputFileInfo.fileName());
    QDateTime local(QDateTime::currentDateTime());
    QString localTimeDate = local.toTimeSpec(Qt::LocalTime)
                                 .toString("yyyy-MM-dd-hh-mm-ss");
    localTimeDate.remove(QChar('-'));
    QString outFileQualifier = QString("-").append(inputFileStub
                              .left(inputFileStub.size()-4))
                              .append("-").append(localTimeDate).append(".csv");
    QDir saveDirectory = fileInfo.absolutePath();

// Analysis for faults.
    if (DataProcessingMainUi.faultAnalysisCheckbox->isChecked())
    {
        QString reportFilename = QString("fault").append(outFileQualifier);
        bool header = true;
        if (outfileMessage(reportFilename, &header)) return; // Abort processing
// This will write to the file as created above, or append to the existing file.
        QFile* outFile = new QFile(reportFilename);   // Open file for output
        if (! outFile->open(QIODevice::WriteOnly | QIODevice::Append
                                                 | QIODevice::Text))
        {
            displayErrorMessage("Could not open the output file");
            return;
        }
        QTextStream outStream(outFile);

// Build header
        if (header)
        {
            outStream << "Time,";
            outStream << "B1 Op," << "B1 Charge,";
            outStream << "B2 Op," << "B2 Charge,";
            outStream << "B3 Op," << "B3 Charge,";
            outStream << "B1 V," << "B2 V," << "B3 V," << "M1 V,";
            outStream << "Switches," << "Decisions," << "Indicators";
            outStream << "\n\r";
        }
// Read in data from input file
// Skip first line as it may be a header
        inFile->seek(0);      // rewind input file
      	QString lineIn;
        lineIn = inStream.readLine();
        bool startRun = true;
// Index increments by about 0.5 seconds
// To have x-axis in date-time, index must be "double" type, ie ms since epoch.
        double index = 0;
        QDateTime startTime;
        QDateTime previousTime;
        while (! inStream.atEnd())
        {
          	lineIn = inStream.readLine();
            QStringList breakdown = lineIn.split(",");
            int size = breakdown.size();
            if (size == LINE_WIDTH)
            {
                QDateTime time = QDateTime::fromString(breakdown[0].simplified(),Qt::ISODate);
                if (time.isValid())
                {
// On the first run get the start time
                    if (startRun)
                    {
                        startRun = false;
                        startTime = time;
                        previousTime = startTime;
                        index = startTime.toMSecsSinceEpoch();
                    }
// Try to keep index and time in sync to account for jumps in time.
// Index is counting half seconds and time from records is integer seconds only.
                    if (previousTime == time) index += 500;
                    else index = time.toMSecsSinceEpoch();

// Analyse for faults.
// Look for charger not allocated but not all batteries in float or rest.
                    float battery1Voltage = breakdown[2].simplified().toFloat();
                    float battery2Voltage = breakdown[8].simplified().toFloat();
                    float battery3Voltage = breakdown[14].simplified().toFloat();
                    float panel1Voltage = breakdown[24].simplified().toFloat();
                    QString opState1 = breakdown[4].simplified();
                    QString opState2 = breakdown[10].simplified();
                    QString opState3 = breakdown[16].simplified();
                    QString chargeState1 = breakdown[5].simplified();
                    QString chargeState2 = breakdown[11].simplified();
                    QString chargeState3 = breakdown[17].simplified();
                    QString chargeMode1 = breakdown[6].simplified();
                    QString chargeMode2 = breakdown[12].simplified();
                    QString chargeMode3 = breakdown[18].simplified();
                    if ((opState1 != "Charge") &&
                        (opState2 != "Charge") &&
                        (opState3 != "Charge") &&
                        (((chargeMode1 != "Float") && (chargeMode1 != "Rest")
                                                       && (panel1Voltage > battery1Voltage)) ||
                        ((chargeMode2 != "Float") && (chargeMode2 != "Rest")
                                                       && (panel1Voltage > battery2Voltage)) ||
                        ((chargeMode3 != "Float") && (chargeMode3 != "Rest")
                                                       && (panel1Voltage > battery3Voltage))))
                    {
                        outStream << breakdown[0].simplified() << ",";
                        outStream << opState1 << ",";
                        outStream << chargeMode1 << ",";
                        outStream << opState2 << ",";
                        outStream << chargeMode2 << ",";
                        outStream << opState3 << ",";
                        outStream << chargeMode3 << ",";
                        outStream << battery1Voltage << ",";
                        outStream << battery2Voltage << ",";
                        outStream << battery3Voltage << ",";
                        outStream << panel1Voltage << ",";
                        outStream << breakdown[27].simplified() << ",";
                        outStream << breakdown[28].simplified() << ",";
                        outStream << breakdown[29].simplified();
    //                    outStream << lineIn;
                        outStream << "\n\r";
                    }
                }
            }
        }
        outFile->close();
        delete outFile;
    }
// Analyse file to extract battery charger data. Each battery is treated
// independently and data for each is printed to a different output file.
// Values of voltage and current are extracted following a short settling
// period after the charger is applied. Charge state is also given.
    if (DataProcessingMainUi.chargerAnalysisCheckbox->isChecked())
    {
        for (int i=0; i<3; i++)
        {
            QString reportFilename = QString("charging-B%1").arg(i)
                                     .append(outFileQualifier);
            bool header = true;
            if (outfileMessage(reportFilename, &header)) return; // Abort processing
// This will write to the file as created above, or append to the existing file.
            QFile* outFile = new QFile(reportFilename);   // Open file for output
            if (! outFile->open(QIODevice::WriteOnly | QIODevice::Append
                                                     | QIODevice::Text))
            {
                displayErrorMessage("Could not open the output file");
                return;
            }
            QTextStream outStream(outFile);

// Build header
            if (header)
            {
                outStream << "Time,";
                outStream << "Mode,";
                outStream << "V," << "I,";
                outStream << "\n\r";
            }
// Read in data from input file
// Skip first line as it may be a header
            inFile->seek(0);                // rewind input file
          	QString lineIn;
            lineIn = inStream.readLine();
            while (! inStream.atEnd())
            {
              	lineIn = inStream.readLine();
                QStringList breakdown = lineIn.split(",");
                int size = breakdown.size();
                if (size == LINE_WIDTH)
                {
                    QDateTime time = QDateTime::fromString(breakdown[0].simplified(),Qt::ISODate);
                    if (time.isValid())
                    {

// Look for charger allocated and battery not in rest
                        float batteryVoltage = breakdown[2+6*i].simplified().toFloat();
                        float batteryCurrent = breakdown[1+6*i].simplified().toFloat();
                        QString opState = breakdown[4+6*i].simplified();
                        QString chargeState = breakdown[5+6*i].simplified();
                        QString chargeMode = breakdown[6+6*i].simplified();
                        if ((opState == "Charge") &&
                            (chargeMode != "Rest"))
                        {
                            outStream << breakdown[0].simplified() << ",";
                            outStream << chargeMode << ",";
                            outStream << batteryVoltage << ",";
                            outStream << batteryCurrent;
                            outStream << "\n\r";
                        }
                    }
                }
            }
            outFile->close();
            delete outFile;
        }
    }
// Analyse file to extract solar current data from all batteries.
// This is extracted when a battery is in bulk charge.
// Do not output the first record when a battery enters bulk charge to avoid
// inaccuracies due to delays between the state change and the measurement.
    if (DataProcessingMainUi.solarAnalysisCheckbox->isChecked())
    {
        bool firstRecord = true;
        QString reportFilename = QString("solar").append(outFileQualifier);
        bool header = true;
        if (outfileMessage(reportFilename, &header)) return; // Abort processing
// This will write to the file as created above, or append to the existing file.
        QFile* outFile = new QFile(reportFilename);   // Open file for output
        if (! outFile->open(QIODevice::WriteOnly | QIODevice::Append
                                                 | QIODevice::Text))
        {
            displayErrorMessage("Could not open the output file");
            return;
        }
        QTextStream outStream(outFile);

// Build header
        if (header)
        {
            outStream << "Time,";
            outStream << "V," << "I,";
            outStream << "\n\r";
        }
// Read in data from input file
// Skip first line as it may be a header
        inFile->seek(0);                // rewind input file
      	QString lineIn;
        lineIn = inStream.readLine();
        while (! inStream.atEnd())
        {
          	lineIn = inStream.readLine();
            QStringList breakdown = lineIn.split(",");
            int size = breakdown.size();
            if (size == LINE_WIDTH)
            {
                QDateTime time = QDateTime::fromString(breakdown[0].simplified(),Qt::ISODate);
                if (time.isValid())
                {

// Look for charger allocated and battery in bulk
                    float battery1Voltage = breakdown[2].simplified().toFloat();
                    float battery2Voltage = breakdown[8].simplified().toFloat();
                    float battery3Voltage = breakdown[14].simplified().toFloat();
                    float battery1Current = breakdown[1].simplified().toFloat();
                    float battery2Current = breakdown[7].simplified().toFloat();
                    float battery3Current = breakdown[13].simplified().toFloat();
                    QString op1State = breakdown[4].simplified();
                    QString op2State = breakdown[10].simplified();
                    QString op3State = breakdown[16].simplified();
                    QString charge1Mode = breakdown[6].simplified();
                    QString charge2Mode = breakdown[12].simplified();
                    QString charge3Mode = breakdown[18].simplified();
                    if (firstRecord) firstRecord = false;
                    else
                    {
                        if ((op1State == "Charge") && (charge1Mode == "Bulk"))
                        {
                            outStream << breakdown[0].simplified() << ",";
                            outStream << battery1Voltage << ",";
                            outStream << battery1Current;
                            outStream << "\n\r";
                        }
                        else if ((op2State == "Charge") && (charge2Mode == "Bulk"))
                        {
                            outStream << breakdown[0].simplified() << ",";
                            outStream << battery2Voltage << ",";
                            outStream << battery2Current;
                            outStream << "\n\r";
                        }
                        else if ((op3State == "Charge") && (charge3Mode == "Bulk"))
                        {
                            outStream << breakdown[0].simplified() << ",";
                            outStream << battery3Voltage << ",";
                            outStream << battery3Current;
                            outStream << "\n\r";
                        }
                        else firstRecord = true;
                    }
                }
            }
        }
        outFile->close();
        delete outFile;
    }
}

//-----------------------------------------------------------------------------
/** @brief Extract and Combine Raw Records to CSV.

Raw records are combined into single records for each time interval, and written
to a csv file. Format suitable for spreadsheet analysis.

@param[in] QDateTime start time.
@param[in] QDateTime end time.
@param[in] QFile* input file.
@param[in] QFile* output file.
*/

bool DataProcessingGui::combineRecords(QDateTime startTime, QDateTime endTime,
                                       QFile* inFile, QFile* outFile,bool header)
{
    int battery1Voltage = -1;
    int battery1Current = 0;
    int battery1SoC = -1;
    QString battery1StateText;
    QString battery1FillText;
    QString battery1ChargeText;
    int battery2Voltage = -1;
    int battery2Current = 0;
    int battery2SoC = -1;
    QString battery2StateText;
    QString battery2FillText;
    QString battery2ChargeText;
    int battery3Voltage = -1;
    int battery3Current = 0;
    int battery3SoC = -1;
    QString battery3StateText;
    QString battery3FillText;
    QString battery3ChargeText;
    int load1Voltage = -1;
    int load1Current = 0;
    int load2Voltage = -1;
    int load2Current = 0;
    int panel1Voltage = -1;
    int panel1Current = 0;
    int temperature = -1;
    QString controls = "     ";
    QString switches;
    QString decision;
    QString indicatorString;
    int debug1a = -1;
    int debug2a = -1;
    int debug3a = -1;
    int debug1b = -1;
    int debug2b = -1;
    int debug3b = -1;
    bool blockStart = false;
    QTextStream inStream(inFile);
    QTextStream outStream(outFile);
    if (header)
    {
        outStream << "Time,";
        outStream << "B1 I," << "B1 V," << "B1 Cap," << "B1 Op," << "B1 State," << "B1 Charge,";
        outStream << "B2 I," << "B2 V," << "B2 Cap," << "B2 Op," << "B2 State," << "B2 Charge,";
        outStream << "B3 I," << "B3 V," << "B3 Cap," << "B3 Op," << "B3 State," << "B3 Charge,";
        outStream << "L1 I," << "L1 V," << "L2 I," << "L2 V," << "M1 I," << "M1 V,";
        outStream << "Temp," << "Controls," << "Switches," << "Decisions," << "Indicators,";
        outStream << "Debug 1a," << "Debug 1b," << "Debug 2a," << "Debug 2b," << "Debug 3a," << "Debug 3b";
        outStream << "\n\r";
    }
    QDateTime time = startTime;
    while (! inStream.atEnd())
    {
        if  (time > endTime) break;
      	QString lineIn = inStream.readLine();
        QStringList breakdown = lineIn.split(",");
        int size = breakdown.size();
        if (size <= 0) break;
        QString firstText = breakdown[0].simplified();
        QString secondText;
        QString thirdText;
        int secondField = -1;
        if (size > 1)
        {
            secondText = breakdown[1].simplified();
            secondField = secondText.toInt();
        }
        int thirdField = -1;
        if (size > 2)
        {
            thirdText = breakdown[2].simplified();
            thirdField = thirdText.toInt();
        }
        if (size > 1)
        {
// Find and extract the time record
            if (firstText == "pH")
            {
                time = QDateTime::fromString(breakdown[1].simplified(),Qt::ISODate);
                if ((blockStart) && (time > startTime))
                {
                    outStream << timeRecord << ",";
                    outStream << (float)battery1Current/256 << ",";
                    outStream << (float)battery1Voltage/256 << ",";
                    outStream << (float)battery1SoC/256 << ",";
                    outStream << battery1StateText << ",";
                    outStream << battery1FillText << ",";
                    outStream << battery1ChargeText << ",";
                    outStream << (float)battery2Current/256 << ",";
                    outStream << (float)battery2Voltage/256 << ",";
                    outStream << (float)battery2SoC/256 << ",";
                    outStream << battery2StateText << ",";
                    outStream << battery2FillText << ",";
                    outStream << battery2ChargeText << ",";
                    outStream << (float)battery3Current/256 << ",";
                    outStream << (float)battery3Voltage/256 << ",";
                    outStream << (float)battery3SoC/256 << ",";
                    outStream << battery3StateText << ",";
                    outStream << battery3FillText << ",";
                    outStream << battery3ChargeText << ",";
                    outStream << (float)load1Voltage/256 << ",";
                    outStream << (float)load1Current/256 << ",";
                    outStream << (float)load2Voltage/256 << ",";
                    outStream << (float)load2Current/256 << ",";
                    outStream << (float)panel1Voltage/256 << ",";
                    outStream << (float)panel1Current/256 << ",";
                    outStream << (float)temperature/256 << ",";
                    outStream << controls << ",";
                    outStream << switches << ",";
                    outStream << decision << ",";
                    outStream << indicatorString << ",";
                    outStream << debug1a << ",";
                    outStream << debug1b << ",";
                    outStream << debug2a << ",";
                    outStream << debug2b << ",";
                    outStream << debug3a << ",";
                    outStream << debug3b;
                    outStream << "\n\r";
                }
                timeRecord = breakdown[1].simplified();
                blockStart = true;
            }
            if (firstText == "dB1")
            {
                battery1Current = secondField-battery1CurrentZero;
                battery1Voltage = thirdField;
            }
            if (firstText == "dB2")
            {
                battery2Current = secondField-battery2CurrentZero;
                battery2Voltage = thirdField;
            }
            if (firstText == "dB3")
            {
                battery3Current = secondField-battery3CurrentZero;
                battery3Voltage = thirdField;
            }
            if (firstText == "dC1")
            {
                battery1SoC = secondField;
            }
            if (firstText == "dC2")
            {
                battery2SoC = secondField;
            }
            if (firstText == "dC3")
            {
                battery3SoC = secondField;
            }
            if (firstText == "dO1")
            {
                uint battery1State = (secondField & 0x03);
                if (battery1State == 0) battery1StateText = "Loaded";
                else if (battery1State == 1) battery1StateText = "Charge";
                else if (battery1State == 2) battery1StateText = "Isolate";
                else if (battery1State == 3) battery1StateText = "Missing";
                else battery1StateText = "?";
                uint battery1Fill = (secondField >> 2) & 0x03;
                if (battery1Fill == 0) battery1FillText = "Normal";
                else if (battery1Fill == 1) battery1FillText = "Low";
                else if (battery1Fill == 2) battery1FillText = "Critical";
                else if (battery1Fill == 3) battery1FillText = "Faulty";
                else battery1FillText = "?";
                uint battery1Charge = (secondField >> 4) & 0x03;
                if (battery1Charge == 0) battery1ChargeText = "Bulk";
                else if (battery1Charge == 1) battery1ChargeText = "Absorp";
                else if (battery1Charge == 2) battery1ChargeText = "Float";
                else if (battery1Charge == 3) battery1ChargeText = "Rest";
                else battery1ChargeText = "?";
            }
            if (firstText == "dO2")
            {
                uint battery2State = (secondField & 0x03);
                if (battery2State == 0) battery2StateText = "Loaded";
                else if (battery2State == 1) battery2StateText = "Charge";
                else if (battery2State == 2) battery2StateText = "Isolate";
                else if (battery2State == 3) battery2StateText = "Missing";
                else battery2StateText = "?";
                uint battery2Fill = (secondField >> 2) & 0x03;
                if (battery2Fill == 0) battery2FillText = "Normal";
                else if (battery2Fill == 1) battery2FillText = "Low";
                else if (battery2Fill == 2) battery2FillText = "Critical";
                else if (battery2Fill == 3) battery2FillText = "Faulty";
                else battery2FillText = "?";
                uint battery2Charge = (secondField >> 4) & 0x03;
                if (battery2Charge == 0) battery2ChargeText = "Bulk";
                else if (battery2Charge == 1) battery2ChargeText = "Absorp";
                else if (battery2Charge == 2) battery2ChargeText = "Float";
                else if (battery2Charge == 3) battery2ChargeText = "Rest";
                else battery2ChargeText = "?";
            }
            if (firstText == "dO3")
            {
                uint battery3State = (secondField & 0x03);
                if (battery3State == 0) battery3StateText = "Loaded";
                else if (battery3State == 1) battery3StateText = "Charge";
                else if (battery3State == 2) battery3StateText = "Isolate";
                else if (battery3State == 3) battery3StateText = "Missing";
                else battery3StateText = "?";
                uint battery3Fill = (secondField >> 2) & 0x03;
                if (battery3Fill == 0) battery3FillText = "Normal";
                else if (battery3Fill == 1) battery3FillText = "Low";
                else if (battery3Fill == 2) battery3FillText = "Critical";
                else if (battery3Fill == 3) battery3FillText = "Faulty";
                else battery3FillText = "?";
                uint battery3Charge = (secondField >> 4) & 0x03;
                if (battery3Charge == 0) battery3ChargeText = "Bulk";
                else if (battery3Charge == 1) battery3ChargeText = "Absorp";
                else if (battery3Charge == 2) battery3ChargeText = "Float";
                else if (battery3Charge == 3) battery3ChargeText = "Rest";
                else battery3ChargeText = "?";
            }
            if (firstText == "dL1")
            {
                load1Voltage = secondField;
                load1Current = thirdField;
            }
            if (firstText == "dL2")
            {
                load2Voltage = secondField;
                load2Current = thirdField;
            }
            if (firstText == "dM1")
            {
                panel1Voltage = secondField;
                panel1Current = thirdField;
            }
            if (firstText == "dT")
            {
                temperature = secondField;
            }
// A = autotrack, R = recording, M = send measurements,
// D = debug, Charger algorithm, X = load avoidance, I = maintain isolation
            if (firstText == "dD")
            {
                if ((secondField & (1 << 0)) > 0) controls[0] = 'A';
                if ((secondField & (1 << 1)) > 0) controls[1] = 'R';
                if ((secondField & (1 << 3)) > 0) controls[2] = 'M';
                if ((secondField & (1 << 4)) > 0) controls[3] = 'D';
                if (((secondField >> 5) & 3) == 0) controls[4] = '1';
                if (((secondField >> 5) & 3) == 1) controls[4] = '2';
                if (((secondField >> 5) & 3) == 2) controls[4] = '3';
                if ((secondField & (1 << 7)) > 0) controls[5] = 'X';
                if ((secondField & (1 << 8)) > 0) controls[6] = 'I';
            }
// Switch control bits - three 2-bit fields: battery number for each of
// load1, load2 and panel.
            if (firstText == "ds")
            {
                switches.clear();
                uint load1Battery = (secondField >> 0) & 0x03;
                QString load1BatteryText;
                load1BatteryText.setNum(load1Battery);
                if (load1Battery > 0) switches.append(" ").append(load1BatteryText);
                else switches.append(" 0");
                uint load2Battery = (secondField >> 2) & 0x03;
                QString load2BatteryText;
                load2BatteryText.setNum(load2Battery);
                if (load2Battery > 0) switches.append(" ").append(load2BatteryText);
                else switches.append(" 0");
                uint panelBattery = (secondField >> 4) & 0x03;
                QString panelBatteryText;
                panelBatteryText.setNum(panelBattery);
                if (panelBattery > 0) switches.append(" ").append(panelBatteryText);
                else switches.append(" 0");
            }
            if (firstText == "dd")
            {
                bool ok;
                decision = QString("%1").arg(secondText.toInt(&ok),0,16);
            }
            if (firstText == "dI")
            {
                bool ok;
                indicatorString = "";
                int indicators = secondText.toInt(&ok);
                for (int i=0; i<12; i+=2)
                {
                    if ((indicators & (1 << i)) > 0) indicatorString.append("_");
                    else indicatorString.append("O");
                    if ((indicators & (1 << (i+1))) > 0) indicatorString.append("_");
                    else indicatorString.append("U");
                }
            }
            if (firstText == "D1")
            {
                debug1a = secondField;
                if (size > 2) debug1b = thirdField;
            }
            if (firstText == "D2")
            {
                debug2a = secondField;
                if (size > 2) debug2b = thirdField;
            }
            if (firstText == "D3")
            {
                debug3a = secondField;
                if (size > 2) debug3b = thirdField;
            }
        }
    }
    return inStream.atEnd();
}

//-----------------------------------------------------------------------------
/** @brief Seek First Time Record.

The input file is searched record by record until the first time record is
found.

@param[in] QFile* input file.
@returns QDateTime time of first time record. Null if not found.
*/

QDateTime DataProcessingGui::findFirstTimeRecord(QFile* inFile)
{
    QDateTime time;
    QTextStream inStream(inFile);
    while (! inStream.atEnd())
    {
      	QString lineIn = inStream.readLine();
        QStringList breakdown = lineIn.split(",");
        int size = breakdown.size();
        if (size <= 0) break;
        QString firstText = breakdown[0].simplified();
// Find and extract the time record
        if ((size > 1) && (firstText == "pH"))
        {
            time = QDateTime::fromString(breakdown[1].simplified(),Qt::ISODate);
            break;
        }
    }
    return time;
}

//-----------------------------------------------------------------------------
/** @brief Open a Data File for Writing.

This is called from other action functions. The file is requested in a file
dialogue and opened. The function aborts if the file exists.

@returns true if file successfully created and opened.
*/

bool DataProcessingGui::openSaveFile()
{
    if (! saveFile.isEmpty())
    {
        displayErrorMessage("A save file is already open - close first");
        return false;
    }
    QString filename = QFileDialog::getSaveFileName(this,
                        "Save csv Data",
                        QString(),
                        "Comma Separated Variables (*.csv)",0,0);
    if (filename.isEmpty()) return false;
    if (! filename.endsWith(".csv")) filename.append(".csv");
    QFileInfo fileInfo(filename);
    saveDirectory = fileInfo.absolutePath();
    saveFile = saveDirectory.filePath(filename);
    outFile = new QFile(saveFile);             // Open file for output
    if (! outFile->open(QIODevice::WriteOnly))
    {
        displayErrorMessage("Could not open the output file");
        return false;
    }
    return true;
}

//-----------------------------------------------------------------------------
/** @brief Scan the data file

Look for start and end times and record types. Obtain the current zeros from
records that have isolated operational status.

*/

void DataProcessingGui::scanFile(QFile* inFile)
{
    if (! inFile->isOpen()) return;
    QTextStream inStream(inFile);
    QDateTime startTime, endTime;
    uint calibration1Count = 0;
    uint calibration2Count = 0;
    uint calibration3Count = 0;
    int battery1Current = 0;
    int battery2Current = 0;
    int battery3Current = 0;
    battery1CurrentZero = 0;
    battery2CurrentZero = 0;
    battery3CurrentZero = 0;
    while (! inStream.atEnd())
    {
      	QString lineIn = inStream.readLine();
        QStringList breakdown = lineIn.split(",");
        int length = breakdown.size();
        if (length <= 0) break;
        QString firstText = breakdown[0].simplified();
        QString secondText = breakdown[1].simplified();
        if ((firstText == "pH") && (length > 1))
        {
            QDateTime time = QDateTime::fromString(secondText,Qt::ISODate);
            if (startTime.isNull()) startTime = time;
            endTime = time;
        }
        int secondField = secondText.toInt();
        if ((firstText == "dB1") && (length > 1))
        {
            battery1Current = secondField;
        }
        if ((firstText == "dB2") && (length > 1))
        {
            battery2Current = secondField;
        }
        if ((firstText == "dB3") && (length > 1))
        {
            battery3Current = secondField;
        }
        if ((firstText == "dO1") && (length > 1))
        {
            int operationalStatus = secondField&0x03;
            if (operationalStatus == 2)
            {
                calibration1Count++;
                battery1CurrentZero += battery1Current;
            }
        }
        if ((firstText == "dO2") && (length > 1))
        {
            int operationalStatus = secondField&0x03;
            if (operationalStatus == 2)
            {
                calibration2Count++;
                battery2CurrentZero += battery2Current;
            }
        }
        if ((firstText == "dO3") && (length > 1))
        {
            int operationalStatus = secondField&0x03;
            if (operationalStatus == 2)
            {
                calibration3Count++;
                battery3CurrentZero += battery3Current;
            }
        }
    }
// Remove the zero point of current if required
    if (DataProcessingMainUi.zeroCurrentCheckBox->isChecked())  
    {
        battery1CurrentZero /= calibration1Count;
        battery2CurrentZero /= calibration2Count;
        battery3CurrentZero /= calibration3Count;
    }
    else
    {
        battery1CurrentZero = 0;
        battery2CurrentZero = 0;
        battery3CurrentZero = 0;
    }
    if (! startTime.isNull()) DataProcessingMainUi.startTime->setDateTime(startTime);
    if (! endTime.isNull()) DataProcessingMainUi.endTime->setDateTime(endTime);
}

//-----------------------------------------------------------------------------
/** @brief Print an error message.

*/

void DataProcessingGui::displayErrorMessage(QString message)
{
    DataProcessingMainUi.errorMessageLabel->setText(message);
}

//-----------------------------------------------------------------------------
/** @brief Message box for output file exists.

Checks the existence of a specified file and asks for decisions about its
use: abort, overwrite, or append.

@param[in] QString filename: name of output file.
@param[out] bool* append: true if append was selected.
@returns true if abort was selected.
*/

bool DataProcessingGui::outfileMessage(QString filename, bool* append)
{
    QString saveFile = saveDirectory.filePath(filename);
// If report filename exists, decide what action to take.
// Build a message box with options
    if (QFile::exists(saveFile))
    {
        QMessageBox msgBox;
        msgBox.setText(QString("A previous save file ").append(filename).
                        append(" exists."));
// Overwrite the existing file
        QPushButton *overwriteButton = msgBox.addButton(tr("Overwrite"),
                         QMessageBox::AcceptRole);
// Append to the existing file
        QPushButton *appendButton = msgBox.addButton(tr("Append"),
                         QMessageBox::AcceptRole);
// Quit altogether
        QPushButton *abortButton = msgBox.addButton(tr("Abort"),
                         QMessageBox::AcceptRole);
        msgBox.exec();
        if (msgBox.clickedButton() == overwriteButton)
        {
            QFile::remove(saveFile);
        }
        else if (msgBox.clickedButton() == abortButton)
        {
            return true;
        }
// Don't write the header into the appended file
        else if (msgBox.clickedButton() == appendButton)
        {
            *append = true;
        }
    }
    return false;
}

