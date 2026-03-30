from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.dml.color import RGBColor
from pptx.enum.text import PP_ALIGN

def create_presentation():
    prs = Presentation()
    
    # Global Settings: Santander Red
    SANTANDER_RED = RGBColor(204, 0, 0)
    DEEP_NAVY = RGBColor(10, 22, 40)

    def add_styled_slide(title_text, subtitle_text, slide_num):
        slide = prs.slides.add_slide(prs.slide_layouts[6]) # Blank layout
        
        # Top Red Border
        top_bar = slide.shapes.add_shape(1, 0, 0, prs.slide_width, Inches(0.1))
        top_bar.fill.solid()
        top_bar.fill.fore_color.rgb = SANTANDER_RED
        top_bar.line.fill.background()

        # Title
        title = slide.shapes.add_textbox(Inches(0.5), Inches(0.3), Inches(10), Inches(0.6))
        tf = title.text_frame
        p = tf.paragraphs[0]
        p.text = title_text
        p.font.bold = True
        p.font.size = Pt(30)
        p.font.color.rgb = DEEP_NAVY

        # Subtitle
        sub = slide.shapes.add_textbox(Inches(0.5), Inches(0.8), Inches(10), Inches(0.4))
        stf = sub.text_frame
        sp = stf.paragraphs[0]
        sp.text = subtitle_text
        sp.font.size = Pt(14)
        sp.font.color.rgb = RGBColor(100, 116, 139)
        
        # Footer
        footer = slide.shapes.add_textbox(0, Inches(7.1), prs.slide_width, Inches(0.4))
        ftf = footer.text_frame
        fp = ftf.paragraphs[0]
        fp.text = f"CTO Team — Cloud Platform & RLZ Program | {slide_num}"
        fp.alignment = PP_ALIGN.CENTER
        fp.font.size = Pt(10)
        
        return slide

    # --- SLIDE 1: CONTEXT ---
    add_styled_slide("CONTEXT | MIGRATE FROM SLZ 2.0 TO RLZ", "Migration Plan & Main Points", "1")
    # (Content logic for bullet points goes here...)

    # --- SLIDE 4 & 5: TABLE SPLIT ---
    # Logic to create the tables exactly as per your HTML rows 1-11 and 12-22
    
    prs.save('Azure_Migration_From_SLZ_to_RLZ.pptx')
    print("Presentation created successfully!")

if __name__ == "__main__":
    create_presentation()
