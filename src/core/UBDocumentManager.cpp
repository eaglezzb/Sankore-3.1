/*
 * UBDocumentManager.cpp
 *
 *  Created on: Feb 10, 2009
 *      Author: julienbachmann
 */


#include "UBDocumentManager.h"

#include "frameworks/UBStringUtils.h"

#include "adaptors/UBExportFullPDF.h"
#include "adaptors/UBExportDocument.h"
#include "adaptors/UBExportWeb.h"
#include "adaptors/UBWebPublisher.h"
#include "adaptors/UBPowerPointApplication.h"
#include "adaptors/UBImportDocument.h"
#include "adaptors/UBImportPDF.h"
#include "adaptors/UBImportImage.h"

#ifdef Q_OS_WIN
    #include "adaptors/UBImportVirtualPrinter.h"
#endif

#include "domain/UBGraphicsScene.h"
#include "domain/UBGraphicsSvgItem.h"
#include "domain/UBGraphicsPixmapItem.h"

#include "document/UBDocumentProxy.h"

#include "UBApplication.h"
#include "UBSettings.h"
#include "UBPersistenceManager.h"

UBDocumentManager* UBDocumentManager::sDocumentManager = 0;

UBDocumentManager* UBDocumentManager::documentManager()
{
    if (!sDocumentManager)
    {
        sDocumentManager = new UBDocumentManager(qApp);
    }
    return sDocumentManager;
}


UBDocumentManager::UBDocumentManager(QObject *parent)
    :QObject(parent)
{
    // TODO UB 4.7 string used in document persistence (folder names)
    QString dummyImages = tr("images");
    QString dummyVideos = tr("videos");
    QString dummyObjects = tr("objects");
    QString dummyWidgets = tr("widgets");

    UBExportFullPDF* exportFullPdf = new UBExportFullPDF(this);
    mExportAdaptors.append(exportFullPdf);

    UBExportDocument* exportDocument = new UBExportDocument(this);
    mExportAdaptors.append(exportDocument);

//    remove the Publish Documents on Uniboard Web entry
//    UBWebPublisher* webPublisher = new UBWebPublisher(this);
//    mExportAdaptors.append(webPublisher);


#if defined(Q_OS_WIN) || defined(Q_OS_MAC) // TODO UB 4.x - Linux implement a wrapper around Open Office
    UBPowerPointApplication* pptImport = new UBPowerPointApplication(this);
    mImportAdaptors.append(pptImport);
#endif

    UBImportDocument* documentImport = new UBImportDocument(this);
    mImportAdaptors.append(documentImport);
    UBImportPDF* pdfImport = new UBImportPDF(this);
    mImportAdaptors.append(pdfImport);
    UBImportImage* imageImport = new UBImportImage(this);
    mImportAdaptors.append(imageImport);

#ifdef Q_OS_WIN
    UBImportVirtualPrinter* virtualPrinterImport = new UBImportVirtualPrinter(this);
    mImportAdaptors.append(virtualPrinterImport);
#endif
}


UBDocumentManager::~UBDocumentManager()
{
    // NOOP
}


QStringList UBDocumentManager::importFileExtensions()
{
    QStringList result;

    foreach (UBImportAdaptor *importAdaptor, mImportAdaptors)
    {
        result << importAdaptor->supportedExtentions();
    }
    return result;
}


QString UBDocumentManager::importFileFilter()
{
    QString result;

    result += tr("All supported files (*.%1)").arg(importFileExtensions().join(" *."));
    foreach (UBImportAdaptor *importAdaptor, mImportAdaptors)
    {
        if (importAdaptor->importFileFilter().length() > 0)
        {
            if (result.length())
            {
                result += ";;";
            }
            result += importAdaptor->importFileFilter();
        }
    }
    qDebug() << "import file filter" << result;
    return result;
}


UBDocumentProxy* UBDocumentManager::importFile(const QFile& pFile, const QString& pGroup)
{
    QFileInfo fileInfo(pFile);

    UBDocumentProxy* document = 0;

    foreach (UBImportAdaptor *importAdaptor, mImportAdaptors)
    {
        if (importAdaptor->supportedExtentions().lastIndexOf(fileInfo.suffix().toLower()) != -1)
        {
            UBApplication::setDisabled(true);
            document = importAdaptor->importFile(pFile, pGroup);
            UBApplication::setDisabled(false);
        }
    }

    return document;
}


bool UBDocumentManager::addFileToDocument(UBDocumentProxy* pDocument, const QFile& pFile)
{
    QFileInfo fileInfo(pFile);
    foreach (UBImportAdaptor *importAdaptor, mImportAdaptors)
    {
        if (importAdaptor->supportedExtentions().lastIndexOf(fileInfo.suffix().toLower()) != -1)
        {
            UBApplication::setDisabled(true);
            bool result = importAdaptor->addFileToDocument(pDocument, pFile);
            UBApplication::setDisabled(false);
            return result;
        }
    }
    return false;
}


int UBDocumentManager::addImageDirToDocument(const QDir& pDir, UBDocumentProxy* pDocument)
{
    QStringList filenames = pDir.entryList(QDir::Files | QDir::NoDotAndDotDot);

    filenames = UBStringUtils::sortByLastDigit(filenames);

    QStringList fullPathFilenames;

    foreach(QString f, filenames)
    {
        fullPathFilenames << pDir.absolutePath() + "/" + f;
    }

    return addImageAsPageToDocument(fullPathFilenames, pDocument);

}


UBDocumentProxy* UBDocumentManager::importDir(const QDir& pDir, const QString& pGroup)
{
    UBDocumentProxy* doc = UBPersistenceManager::persistenceManager()->createDocument(pGroup, pDir.dirName());

    int result = addImageDirToDocument(pDir, doc);

    if (result > 0)
    {
        doc->setMetaData(UBSettings::documentGroupName, pGroup);
        doc->setMetaData(UBSettings::documentName, pDir.dirName());

        UBPersistenceManager::persistenceManager()->persistDocumentMetadata(doc);

        UBApplication::showMessage(tr("File %1 saved").arg(pDir.dirName()));

    }
    else
    {
        UBPersistenceManager::persistenceManager()->deleteDocument(doc);
    }

    return doc;
}


QList<UBExportAdaptor*> UBDocumentManager::supportedExportAdaptors()
{
    return mExportAdaptors;
}

int UBDocumentManager::addImageAsPageToDocument(const QStringList& filenames, UBDocumentProxy* pDocument)
{

    int result = 0;

    if (filenames.size() > 0)
    {
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

        QApplication::processEvents();

        int pageIndex = pDocument->pageCount();

                if (pageIndex == 1 && UBPersistenceManager::persistenceManager()->loadDocumentScene(pDocument, 0)->isEmpty())
                {
                        pageIndex = 0;
                }

        int expectedPageCount = filenames.size();

        for(int i = 0; i < filenames.size(); i ++)
        {
            UBApplication::showMessage(tr("Importing page %1 of %2").arg(i + 1).arg(expectedPageCount));

            UBGraphicsScene* scene = 0;

            QString fullPath = filenames.at(i);

            QGraphicsItem *gi = 0;

            if (pageIndex == 0)
            {
                scene = UBPersistenceManager::persistenceManager()->loadDocumentScene(pDocument, pageIndex);
            }
            else
            {
                scene = UBPersistenceManager::persistenceManager()->createDocumentSceneAt(pDocument, pageIndex);
            }

            scene->setBackground(false, false);

            if (fullPath.endsWith(".svg") || fullPath.endsWith(".svgz"))
            {
                                gi = scene->addSvg(QUrl::fromLocalFile(fullPath), QPointF(0, 0));
            }
            else
            {
                QPixmap pix(fullPath);

                if (pix.isNull())
                {
                    UBApplication::showMessage(tr("Erronous image data, skipping file %1").arg(filenames.at(i)));
                    expectedPageCount--;
                    continue;
                }
                else
                {
                    gi = scene->addPixmap(pix, QPointF(0, 0));
                }
            }

            if (gi)
            {
                scene->setAsBackgroundObject(gi, true);

                UBPersistenceManager::persistenceManager()->persistDocumentScene(pDocument, scene, pageIndex);

                pageIndex++;
            }

        }

        result = expectedPageCount;

        QApplication::restoreOverrideCursor();

    }

    return result;

}

void UBDocumentManager::emitDocumentUpdated(UBDocumentProxy* pDocument)
{
    emit documentUpdated(pDocument);
}