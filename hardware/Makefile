KIBOT?=kibot
DEBUG?=
OUT_DIR=Generated
EXTRA_OPS=--banner -1 --log $(OUT_DIR)/kibot.log $(DEBUG)
PRJ_SCH=BREAD_Slice.kicad_sch
PRJ_PCB=BREAD_Slice.kicad_pcb

.PHONY: erc drc sch_fab pcb_fab erc_and_fab drc_and_fab

erc:
	$(KIBOT) $(EXTRA_OPS) -e $(PRJ_SCH) -b $(PRJ_PCB) -d $(OUT_DIR) -s drc -i

drc:
	$(KIBOT) $(EXTRA_OPS) -e $(PRJ_SCH) -b $(PRJ_PCB) -d $(OUT_DIR) -s erc -i

sch_fab:
	$(KIBOT) $(EXTRA_OPS) -e $(PRJ_SCH) -b $(PRJ_PCB) -d $(OUT_DIR) \
		print_sch interactive_bom bom_html bom_csv

pcb_fab:
	$(KIBOT) $(EXTRA_OPS) -e $(PRJ_SCH) -b $(PRJ_PCB) -d $(OUT_DIR) \
		print_pcb gerbers excellon_drill gerber_drills position \
		board_top_png board_bottom_png

erc_and_fab:
	$(KIBOT) $(EXTRA_OPS) -e $(PRJ_SCH) -b $(PRJ_PCB) -d $(OUT_DIR) \
		print_sch interactive_bom bom_html bom_csv

drc_and_fab:
	$(KIBOT) $(EXTRA_OPS) -e $(PRJ_SCH) -b $(PRJ_PCB) -d $(OUT_DIR) \
		print_pcb gerbers excellon_drill gerber_drills position
